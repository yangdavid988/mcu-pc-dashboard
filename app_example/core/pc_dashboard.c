#include "core/pc_dashboard.h"
#include "config/sdk_compat.h"
#include <time.h>
#include "config/threshold_config.h"
#include "cJSON.h"
/* lwIP SNTP for time sync (fallback when no PC data) */
/* NOTE: SDK lwipopts.h already maps SNTP_UPDATE_DELAY to sntp_get_update_interval(),
   so the interval is controlled at runtime via sntp_set_update_interval(). */
#include "lwip/apps/sntp.h"
/* Realtek SNTP component (time-of-day thin wrapper, provides set_update_interval) */
#include "sntp/sntp_api.h"
#include "hal/gpio_control.h"
#include "core/standby_manager.h"

/* ========================================================================
 * Global variables
 * ======================================================================== */
PC_Stats_t        g_pc_stats       = { 0 };
volatile bool     g_new_data_ready = false;
volatile bool     g_sht3x_pending  = false;
volatile bool     g_mqtt_connected = false;
volatile uint32_t g_data_last_tick = 0;

/* Internal globals */
static MQTTClient  g_mqtt_client;
static rtos_task_t g_mqtt_task_handle    = NULL;
static bool        g_sht3x_subscribed    = false; /* Whether SHT3X topic has been subscribed */
static bool        g_pc_event_subscribed = false; /* Lock screen event subscription */

#if WEATHER_FETCH_MCU == 0
static bool g_weather_subscribed = false; /* Weather topic subscription */
#endif

/* Lock screen state */
volatile ScreenState_t g_screen_state       = SCREEN_STATE_MONITOR;
volatile bool          g_lock_screen_active = false;
volatile bool          g_pc_event_received  = false; /* first pc/event retained msg processed */

/* ========================================================================
 * cJSON helper — extract uint64_t from parsed JSON tree
 *
 * cJSON stores numbers as double (53-bit mantissa). For values that fit
 * within 53 bits (~9×10¹⁵, i.e. several TB for disk I/O), the double
 * representation is exact. This helper casts directly; if 64-bit fields
 * ever exceed 2^53, switch to raw-text extraction via cJSON_Print.
 * ======================================================================== */
static bool json_get_u64(const cJSON *root, const char *key, uint64_t *value)
{
    if (!root || !key || !value)
        return false;
    const cJSON *item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsNumber(item))
        return false;
    *value = (uint64_t) item->valuedouble;
    return true;
}

/* ========================================================================
 * Unix timestamp to date/time conversion
 * Algorithm: Gregorian calendar (1970 epoch)
 * ======================================================================== */
void unix_to_datetime(uint32_t  timestamp,
                      uint16_t* year,
                      uint8_t*  month,
                      uint8_t*  day,
                      uint8_t*  hour,
                      uint8_t*  min,
                      uint8_t*  sec)
{
    static const uint8_t  days_in_months[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    static const uint16_t days_in_year[2]    = { 365, 366 }; /* 0=common year, 1=leap year */

    uint32_t t = timestamp;

    /* Hours, minutes, seconds */
    if (sec)
        *sec = (uint8_t) (t % 60);
    t /= 60;
    if (min)
        *min = (uint8_t) (t % 60);
    t /= 60;
    if (hour)
        *hour = (uint8_t) (t % 24);
    t /= 24;

    /* Days since 1970-01-01 */
    uint32_t days = t;

    uint16_t y = 1970;
    while (1)
    {
        int      is_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        uint16_t dpy     = days_in_year[is_leap ? 1 : 0];
        if (days < dpy)
            break;
        days -= dpy;
        y++;
    }

    if (year)
        *year = y;

    uint8_t m;
    for (m = 0; m < 12; m++)
    {
        int     is_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        uint8_t dim     = days_in_months[m];
        if (m == 1 && is_leap)
            dim = 29;
        if (days < dim)
            break;
        days -= dim;
    }
    if (month)
        *month = m + 1;
    if (day)
        *day = (uint8_t) (days + 1);
}

/* ========================================================================
 * Byte size formatter
 * ======================================================================== */
void format_bytes(uint64_t bytes, char* out, size_t out_size)
{
    static const char* units[]  = { "B", "KB", "MB", "GB", "TB" };
    int                unit_idx = 0;
    double             val      = (double) bytes;

    while (val >= 1024.0 && unit_idx < 4)
    {
        val /= 1024.0;
        unit_idx++;
    }

    if (unit_idx == 0)
        snprintf(out, out_size, "%.0f %s", val, units[unit_idx]);
    else if (val < 10.0)
        snprintf(out, out_size, "%.1f %s", val, units[unit_idx]);
    else
        snprintf(out, out_size, "%.0f %s", val, units[unit_idx]);
}

/* ========================================================================
 * JSON parser: parse PC stats JSON data into g_pc_stats (via cJSON)
 * ======================================================================== */
static void parse_pc_stats_json(const char* payload)
{
    PC_Stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.has_data = false;

    cJSON *root = cJSON_Parse(payload);
    if (!root)
        return;

    cJSON *item;

/* Helper: extract float field, optional (defaults to 0) */
#define GET_FLOAT(key, field) do {                                  \
        item = cJSON_GetObjectItem(root, key);                      \
        if (cJSON_IsNumber(item)) field = (float) item->valuedouble;\
    } while (0)

/* Helper: extract float field with fallback on missing */
#define GET_FLOAT_OR(key, field, fallback) do {                     \
        item = cJSON_GetObjectItem(root, key);                      \
        if (cJSON_IsNumber(item)) field = (float) item->valuedouble;\
        else                      field = fallback;                 \
    } while (0)

/* Helper: extract string field */
#define GET_STR(key, buf) do {                                      \
        item = cJSON_GetObjectItem(root, key);                      \
        if (cJSON_IsString(item) && item->valuestring) {            \
            strncpy(buf, item->valuestring, sizeof(buf) - 1);       \
            buf[sizeof(buf) - 1] = '\0';                            \
        }                                                           \
    } while (0)

/* Helper: extract bool field */
#define GET_BOOL(key, field) do {                                   \
        item = cJSON_GetObjectItem(root, key);                      \
        if (cJSON_IsBool(item)) field = cJSON_IsTrue(item);         \
    } while (0)

    /* Resource usage */
    GET_FLOAT("cpu",               stats.cpu);
    GET_FLOAT("mem",               stats.mem);
    GET_FLOAT("disk",              stats.disk);
    GET_FLOAT("net_upload_kbps",   stats.net_upload_kbps);
    GET_FLOAT("net_download_kbps", stats.net_download_kbps);

    /* Memory totals (uint64_t via helper) */
    json_get_u64(root, "mem_total", &stats.mem_total);
    json_get_u64(root, "mem_used",  &stats.mem_used);

    /* CPU temperature: may be null */
    item = cJSON_GetObjectItem(root, "cpu_temp");
    stats.cpu_temp_valid = cJSON_IsNumber(item);
    stats.cpu_temp       = stats.cpu_temp_valid ? (float) item->valuedouble : 0.0f;

    /* boot_time (uint64_t → uint32_t) */
    {
        uint64_t bt_u64 = 0;
        if (json_get_u64(root, "boot_time", &bt_u64))
            stats.boot_time = (uint32_t) bt_u64;
    }

    /* Integer fields */
    item = cJSON_GetObjectItem(root, "process_count");
    if (cJSON_IsNumber(item))
        stats.process_count = (uint32_t) item->valuedouble;

    item = cJSON_GetObjectItem(root, "cpu_cores_logical");
    if (cJSON_IsNumber(item))
        stats.cpu_cores_logical = (uint8_t) item->valuedouble;

    item = cJSON_GetObjectItem(root, "cpu_cores_physical");
    if (cJSON_IsNumber(item))
        stats.cpu_cores_physical = (uint8_t) item->valuedouble;

    /* timestamp (uint64_t → uint32_t) */
    {
        uint64_t ts_u64 = 0;
        if (json_get_u64(root, "timestamp", &ts_u64))
            stats.timestamp = (uint32_t) ts_u64;
    }

    /* Battery */
    GET_FLOAT("battery_percent", stats.battery_percent);
    GET_BOOL("battery_plugged",  stats.battery_plugged);

    /* Disk I/O (uint64_t) */
    json_get_u64(root, "disk_read_bytes",  &stats.disk_read_bytes);
    json_get_u64(root, "disk_write_bytes", &stats.disk_write_bytes);

    /* Username */
    GET_STR("current_user", stats.current_user);

    /* ===== V2 field parsers ===== */

    /* CPU frequency (negative = unavailable) */
    GET_FLOAT_OR("cpu_freq_current", stats.cpu_freq_current, -1.0f);
    GET_FLOAT_OR("cpu_freq_min",     stats.cpu_freq_min,     -1.0f);
    GET_FLOAT_OR("cpu_freq_max",     stats.cpu_freq_max,     -1.0f);

    /* Hostname / OS */
    GET_STR("hostname",    stats.hostname);
    GET_STR("os_platform", stats.os_platform);

    /* Swap */
    GET_FLOAT("swap_percent", stats.swap_percent);
    json_get_u64(root, "swap_total", &stats.swap_total);
    json_get_u64(root, "swap_used",  &stats.swap_used);

    /* GPU info (negative = unavailable) */
    GET_STR("gpu_name", stats.gpu_name);
    GET_FLOAT_OR("gpu_usage",        stats.gpu_usage,        -1.0f);
    GET_FLOAT_OR("gpu_mem_used_mb",  stats.gpu_mem_used_mb,  -1.0f);
    GET_FLOAT_OR("gpu_mem_total_mb", stats.gpu_mem_total_mb, -1.0f);
    GET_FLOAT_OR("gpu_temp_c",       stats.gpu_temp_c,       -1.0f);

    /* Disk I/O utilization */
    GET_FLOAT_OR("disk_io_percent",  stats.disk_io_percent,  -1.0f);

#undef GET_FLOAT
#undef GET_FLOAT_OR
#undef GET_STR
#undef GET_BOOL

    stats.has_data = true;

    /* Free cJSON tree before critical section — minimise interrupt-disabled time */
    cJSON_Delete(root);

    /* Sample timestamp BEFORE critical section — rtos_time_* may use mutex/spinlock
     * which cannot be acquired with interrupts disabled. */
    uint32_t now_tick = rtos_time_get_current_system_time_ms();

    /* Atomic global update (disable interrupts to prevent LVGL thread from reading partial state) */
    taskENTER_CRITICAL();
    memcpy(&g_pc_stats, &stats, sizeof(PC_Stats_t));
    g_new_data_ready = true;
    g_data_last_tick = now_tick;
    taskEXIT_CRITICAL();
}

/* ========================================================================
 * Lock screen event parser — topic "pc/event"
 * Payload: {"event": "lock", "timestamp": 1234567890}
 * ======================================================================== */
static void parse_lock_event(const char* payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (!root)
        return;

    cJSON *item = cJSON_GetObjectItem(root, "event");
    if (cJSON_IsString(item) && item->valuestring)
    {
        if (strcmp(item->valuestring, "lock") == 0)
        {
            standby_enter();
            RTK_LOGI(TAG, "Lock event received -> CLOCK mode\n");
        }
        else if (strcmp(item->valuestring, "unlock") == 0)
        {
            standby_exit();
            RTK_LOGI(TAG, "Unlock event received -> MONITOR mode\n");
        }
    }

    cJSON_Delete(root);
}

#if WEATHER_FETCH_MCU == 0
/* ========================================================================
 * Weather JSON parser — topic "pc/weather"
 * Payload (flat keys, published with retain=True):
 *   {"weather_temp_c":25.3, "weather_humidity":65, "weather_wind_speed":3.5,
 *    "weather_condition_code":800, "weather_description":"clear sky",
 *    "weather_city":"Gusu,Jiangsu"}
 * ======================================================================== */
static void parse_weather_json(const char* payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (!root)
        return;

    cJSON *item;
    float w_temp   = 0.0f;
    float w_humi_f = 0.0f;
    float w_wind   = 0.0f;
    char  w_desc[WEATHER_DESC_MAX_LEN] = { 0 };
    char  w_city[WEATHER_CITY_MAX_LEN] = { 0 };

    /* weather_temp_c is mandatory */
    item = cJSON_GetObjectItem(root, "weather_temp_c");
    if (!cJSON_IsNumber(item))
    {
        cJSON_Delete(root);
        return;
    }
    w_temp = (float) item->valuedouble;

    item = cJSON_GetObjectItem(root, "weather_humidity");
    if (cJSON_IsNumber(item))
        w_humi_f = (float) item->valuedouble;

    item = cJSON_GetObjectItem(root, "weather_wind_speed");
    if (cJSON_IsNumber(item))
        w_wind = (float) item->valuedouble;

    item = cJSON_GetObjectItem(root, "weather_description");
    if (cJSON_IsString(item) && item->valuestring)
        strncpy(w_desc, item->valuestring, sizeof(w_desc) - 1);

    item = cJSON_GetObjectItem(root, "weather_city");
    if (cJSON_IsString(item) && item->valuestring)
        strncpy(w_city, item->valuestring, sizeof(w_city) - 1);

    cJSON_Delete(root);

    weather_update_from_mqtt(w_temp, (int) w_humi_f, w_wind,
                             w_desc[0] ? w_desc : NULL,
                             w_city[0] ? w_city : NULL);
}
#endif /* WEATHER_FETCH_MCU == 0 */

/* ========================================================================
 * SHT3X JSON parser (via cJSON)
 * ======================================================================== */
static void parse_sht3x_json(const char* payload)
{
    float temp_val_c = 0.0f, temp_val_f = 0.0f, humi_val = 0.0f;
    bool  temp_ok = false, temp_f_ok = false, humi_ok = false;

    cJSON *root = cJSON_Parse(payload);
    if (!root)
        return;

    cJSON *item;

    item = cJSON_GetObjectItem(root, "temperature_C");
    if (cJSON_IsNumber(item))
    {
        temp_val_c = (float) item->valuedouble;
        temp_ok    = true;
    }

    item = cJSON_GetObjectItem(root, "temperature_F");
    if (cJSON_IsNumber(item))
    {
        temp_val_f = (float) item->valuedouble;
        temp_f_ok  = true;
    }

    item = cJSON_GetObjectItem(root, "humidity");
    if (cJSON_IsNumber(item))
    {
        humi_val = (float) item->valuedouble;
        humi_ok  = true;
    }

    cJSON_Delete(root);

    if (!temp_ok && !humi_ok)
    {
        return;
    }

    taskENTER_CRITICAL();
    /* Read current values for threshold comparison */
    float cur_temp = g_pc_stats.sht3x_temperature;
    float cur_humi = g_pc_stats.sht3x_humidity;
    bool  had_data = g_pc_stats.sht3x_valid;

    /* Always update storage with latest values */
    if (temp_ok)
        g_pc_stats.sht3x_temperature = temp_val_c;
    if (temp_f_ok)
        g_pc_stats.sht3x_temperature_f = temp_val_f;
    if (humi_ok)
        g_pc_stats.sht3x_humidity = humi_val;
    g_pc_stats.sht3x_valid = true;
    g_pc_stats.has_data    = true;

    /* Only trigger a UI refresh if change exceeds threshold — tiny
     * fluctuations (e.g. ±0.1°C) are discarded to avoid unnecessary
     * data refreshes that contribute to multi-rect tearing.         */
    {
        float d_temp          = (temp_ok) ? ((temp_val_c > cur_temp) ? (temp_val_c - cur_temp) : (cur_temp - temp_val_c)) : 0.0f;
        float d_humi          = (humi_ok) ? ((humi_val > cur_humi) ? (humi_val - cur_humi) : (cur_humi - humi_val)) : 0.0f;
        bool  above_threshold = !had_data ||
                                d_temp >= SHT3X_THRESHOLD_TEMP_C ||
                                d_humi >= SHT3X_THRESHOLD_HUMI_PCT;
        if (above_threshold)
        {
            g_sht3x_pending = true; /* Don't set g_new_data_ready — wait for next JSON refresh sync */
        }
    }
    taskEXIT_CRITICAL();
}

/* ========================================================================
 * MQTT message arrival callback (routes by topic)
 * ======================================================================== */
static void messageArrived(MessageData* data, void* discard)
{
    (void) discard;

    char* topic       = data->topicName->lenstring.data;
    int   topic_len   = data->topicName->lenstring.len;
    char* payload     = (char*) data->message->payload;
    int   payload_len = data->message->payloadlen;

    /* Copy payload as C string for parsing.
     * Static buffer: 2048 bytes on stack would strain MQTT task (8192B stack).
     * Reuse same buffer across invocations since this runs in MQTT task context only. */
    static char json_buf[JSON_PARSE_BUF_SIZE];
    int         copy_len = payload_len;
    if (copy_len >= (int) sizeof(json_buf))
    {
        copy_len = sizeof(json_buf) - 1;
        mqtt_printf(MQTT_INFO,
                    "DIAG: msg truncated! payload_len=%d buf=%u\n",
                    payload_len,
                    (unsigned int) sizeof(json_buf));
    }
    memcpy(json_buf, payload, copy_len);
    json_buf[copy_len] = '\0';

    /* Route by topic */
    if (topic_len == (int) strlen(MQTT_TOPIC_PC_STATS) &&
        strncmp(topic, MQTT_TOPIC_PC_STATS, topic_len) == 0)
    {
        /* Standby (CLOCK) mode: skip data updates entirely — weather, stats,
         * and sensor processing consume CPU and set g_new_data_ready which is
         * wasted when the UI is showing the clock face. Only keep processing
         * pc/event to detect UNLOCK transition.                          */
        if (g_screen_state != SCREEN_STATE_CLOCK)
        {
            parse_pc_stats_json(json_buf);
        }
    }
    else if (topic_len == (int) strlen(MQTT_TOPIC_SHT3X) &&
             strncmp(topic, MQTT_TOPIC_SHT3X, topic_len) == 0)
    {
        if (g_screen_state != SCREEN_STATE_CLOCK)
        {
            parse_sht3x_json(json_buf);
        }
    }
    else if (topic_len == (int) strlen(MQTT_TOPIC_PC_EVENT) &&
             strncmp(topic, MQTT_TOPIC_PC_EVENT, topic_len) == 0)
    {
        parse_lock_event(json_buf);
    }
#if WEATHER_FETCH_MCU == 0
    else if (topic_len == (int) strlen(MQTT_TOPIC_WEATHER) &&
             strncmp(topic, MQTT_TOPIC_WEATHER, topic_len) == 0)
    {
        /* Standby: lock status is the only MQTT data we process — weather
         * (like stats and SHT3X) is skipped to save CPU. The retained
         * message delivered on subscribe is handled before the retained
         * lock (subscription order: weather → event), so the very first
         * snapshot always reaches g_weather regardless of CLOCK mode.  */
        if (g_screen_state != SCREEN_STATE_CLOCK)
        {
            parse_weather_json(json_buf);
        }
    }
#endif /* WEATHER_FETCH_MCU == 0 */
    else
    {
        /* Silently ignore unmatched topics */
    }
}

/* ========================================================================
 * Reset g_pc_stats to defaults (called on MQTT disconnect)
 * ======================================================================== */
void pc_stats_reset_to_default(void)
{
    PC_Stats_t empty;
    memset(&empty, 0, sizeof(empty));

    /* Set N/A fields to negative values */
    empty.cpu_freq_current = -1.0f;
    empty.cpu_freq_min     = -1.0f;
    empty.cpu_freq_max     = -1.0f;
    empty.gpu_usage        = -1.0f;
    empty.gpu_mem_used_mb  = -1.0f;
    empty.gpu_mem_total_mb = -1.0f;
    empty.gpu_temp_c       = -1.0f;
    empty.disk_io_percent  = -1.0f;
    empty.cpu_temp_valid   = false;
    empty.sht3x_valid      = false;
    empty.has_data         = false;
    /* battery_percent = 0 means N/A (see Issue 3: threshold range (0, 100]) */

    taskENTER_CRITICAL();
    memcpy(&g_pc_stats, &empty, sizeof(PC_Stats_t));
    g_new_data_ready = true;
    taskEXIT_CRITICAL();
}

/* ========================================================================
 * MQTT main task
 * ======================================================================== */
void pc_dashboard_task(void* parameters)
{
    (void) parameters;

    Network                network;
    unsigned char          sendbuf[MQTT_SENDBUF_SIZE];
    unsigned char          readbuf[MQTT_READBUF_SIZE];
    int                    rc          = 0;
    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
    const char*            address     = MQTT_BROKER_ADDRESS;

    /* MQTT connection params: subscribe only, no Will message */
    connectData.MQTTVersion      = 3;
    connectData.clientID.cstring = (char*) MQTT_CLIENT_ID;
    connectData.willFlag         = 0;

    memset(readbuf, 0x00, sizeof(readbuf));

    RTK_LOGI(TAG, "Wait Wi-Fi to be connected...\n");

    /* Wait for Wi-Fi connection */
    while (COMPAT_CHECK_CONNECTIVITY(NETIF_WLAN_STA_INDEX) != CONNECTION_VALID)
    {
        rtos_time_delay_ms(2000);
    }

    RTK_LOGI(TAG, "Wi-Fi connected.\n");

    /* ---- Initialize SNTP for time sync (fallback when no PC data) ---- */
    /* NOTE: interval controlled at runtime via SDK's sntp_get_update_interval() mapping */
    sntp_set_update_interval(86400000); /* re-sync every 24h (Realtek wrapper) */
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_init();
    RTK_LOGI(TAG, "SNTP initialized (server: ntp.aliyun.com)\n");

    /* Network / MQTT client initialization */
    NetworkInit(&network);
    network.use_ssl              = 1;
    connectData.username.cstring = (char*) MQTT_USERNAME;
    connectData.password.cstring = (char*) MQTT_PASSWORD;

    MQTTClientInit(&g_mqtt_client,
                   &network,
                   30000,
                   sendbuf,
                   sizeof(sendbuf),
                   readbuf,
                   sizeof(readbuf));

    g_mqtt_client.mqttstatus = MQTT_START;

    /* Main loop */
    while (1)
    {
        fd_set         read_fds;
        fd_set         except_fds;
        struct timeval timeout;

        FD_ZERO(&read_fds);
        FD_ZERO(&except_fds);

        timeout.tv_sec  = MQTT_SELECT_TIMEOUT;
        timeout.tv_usec = 0;

        if (network.my_socket >= 0)
        {
            FD_SET(network.my_socket, &read_fds);
            FD_SET(network.my_socket, &except_fds);
        }

        rc = FreeRTOS_Select(network.my_socket + 1,
                             &read_fds,
                             NULL,
                             &except_fds,
                             &timeout);
        if (rc < 0)
        {
            mqtt_printf(MQTT_INFO,
                        "FreeRTOS_Select failed, rc=%d\n",
                        rc);
        }

        if (FD_ISSET(network.my_socket, &except_fds))
        {
            mqtt_printf(MQTT_INFO, "except_fds set, reconnecting...\n");
            MQTTSetStatus(&g_mqtt_client, MQTT_START);
        }

        /* MQTT state machine (connect, subscribe, receive) */
        MQTTDataHandle(&g_mqtt_client,
                       &read_fds,
                       &connectData,
                       messageArrived,
                       (char*) address,
                       (char*) MQTT_SUB_TOPIC);

        /* Update MQTT connection status for UI layer */
        g_mqtt_connected = (g_mqtt_client.mqttstatus == MQTT_RUNNING);

        /* After main subscription is active, subscribe to SHT3X topic separately.
         *
         * Note: In MQTT_TASK mode, MQTTSubscribe() only sends the SUBSCRIBE packet
         * but does not register the callback in the messageHandlers array
         * (WAIT_FOR_ACK not defined causes handler registration code to be skipped).
         * Therefore we need to manually write the handler into an empty slot.
         */
        if (g_mqtt_client.mqttstatus == MQTT_RUNNING && !g_sht3x_subscribed)
        {
            RTK_LOGI(TAG, "Subscribing to SHT3X topic: %s\n", MQTT_SUB_TOPIC_SHT3X);
            int sub_rc = MQTTSubscribe(&g_mqtt_client,
                                       MQTT_SUB_TOPIC_SHT3X,
                                       QOS0,
                                       messageArrived);

            /* MQTT_TASK mode: MQTTSubscribe() doesn't register handler, do it manually */
            if (sub_rc == 0)
            {
                int  i;
                bool already_registered = false;
                for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
                {
                    if (g_mqtt_client.messageHandlers[i].topicFilter != NULL &&
                        strcmp(g_mqtt_client.messageHandlers[i].topicFilter,
                               MQTT_SUB_TOPIC_SHT3X) == 0)
                    {
                        already_registered = true;
                        break;
                    }
                }
                if (!already_registered)
                {
                    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
                    {
                        if (g_mqtt_client.messageHandlers[i].topicFilter == NULL)
                        {
                            g_mqtt_client.messageHandlers[i].topicFilter =
                                MQTT_SUB_TOPIC_SHT3X;
                            g_mqtt_client.messageHandlers[i].fp =
                                messageArrived;
                            break;
                        }
                    }
                }
            }

            g_sht3x_subscribed = true;
        }
        else if (g_mqtt_client.mqttstatus != MQTT_RUNNING)
        {
            g_sht3x_subscribed = false; /* Reset on disconnect for re-subscribe on reconnect */
        }

#if WEATHER_FETCH_MCU == 0
        /* Subscribe to weather topic (BEFORE pc/event so retained weather arrives
         * while g_screen_state is still MONITOR, before retained lock triggers CLOCK) */
        if (g_mqtt_client.mqttstatus == MQTT_RUNNING && !g_weather_subscribed)
        {
            RTK_LOGI(TAG, "Subscribing to weather topic: %s\n", MQTT_SUB_TOPIC_WEATHER);
            int sub_rc = MQTTSubscribe(&g_mqtt_client,
                                       MQTT_SUB_TOPIC_WEATHER,
                                       QOS0,
                                       messageArrived);

            if (sub_rc == 0)
            {
                int  i;
                bool already_registered = false;
                for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
                {
                    if (g_mqtt_client.messageHandlers[i].topicFilter != NULL &&
                        strcmp(g_mqtt_client.messageHandlers[i].topicFilter,
                               MQTT_SUB_TOPIC_WEATHER) == 0)
                    {
                        already_registered = true;
                        break;
                    }
                }
                if (!already_registered)
                {
                    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
                    {
                        if (g_mqtt_client.messageHandlers[i].topicFilter == NULL)
                        {
                            g_mqtt_client.messageHandlers[i].topicFilter =
                                MQTT_SUB_TOPIC_WEATHER;
                            g_mqtt_client.messageHandlers[i].fp =
                                messageArrived;
                            break;
                        }
                    }
                }
            }

            g_weather_subscribed = true;
        }
        else if (g_mqtt_client.mqttstatus != MQTT_RUNNING)
        {
            g_weather_subscribed = false;
        }
#endif

        /* Subscribe to lock screen event topic */
        if (g_mqtt_client.mqttstatus == MQTT_RUNNING && !g_pc_event_subscribed)
        {
            RTK_LOGI(TAG, "Subscribing to lock event topic: %s\n", MQTT_SUB_TOPIC_EVENT);
            int sub_rc = MQTTSubscribe(&g_mqtt_client,
                                       MQTT_SUB_TOPIC_EVENT,
                                       QOS0,
                                       messageArrived);

            if (sub_rc == 0)
            {
                int  i;
                bool already_registered = false;
                for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
                {
                    if (g_mqtt_client.messageHandlers[i].topicFilter != NULL &&
                        strcmp(g_mqtt_client.messageHandlers[i].topicFilter,
                               MQTT_SUB_TOPIC_EVENT) == 0)
                    {
                        already_registered = true;
                        break;
                    }
                }
                if (!already_registered)
                {
                    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
                    {
                        if (g_mqtt_client.messageHandlers[i].topicFilter == NULL)
                        {
                            g_mqtt_client.messageHandlers[i].topicFilter =
                                MQTT_SUB_TOPIC_EVENT;
                            g_mqtt_client.messageHandlers[i].fp =
                                messageArrived;
                            break;
                        }
                    }
                }
            }

            g_pc_event_subscribed = true;
        }
        else if (g_mqtt_client.mqttstatus != MQTT_RUNNING)
        {
            g_pc_event_subscribed = false;
        }
    }

    g_mqtt_task_handle = NULL;
    rtos_task_delete(NULL);
}

/* ========================================================================
 * External interface: start / stop
 * ======================================================================== */
void pc_dashboard_start(void)
{
    if (g_mqtt_task_handle != NULL)
    {
        RTK_LOGI(TAG, "PC dashboard already running.\n");
        return;
    }

    if (rtos_task_create(&g_mqtt_task_handle,
                         "pc_dashboard_task",
                         pc_dashboard_task,
                         NULL,
                         TASK_STACK_MQTT,
                         tskIDLE_PRIORITY + 2) != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "Create PC dashboard task failed.\n");
        g_mqtt_task_handle = NULL;
    }
    else
    {
        RTK_LOGI(TAG, "PC dashboard task started.\n");
    }
}

void pc_dashboard_stop(void)
{
    if (g_mqtt_task_handle == NULL)
    {
        RTK_LOGI(TAG, "PC dashboard not running.\n");
        return;
    }

    RTK_LOGI(TAG, "Stopping PC dashboard...\n");

    if ((g_mqtt_client.ipstack != NULL) &&
        (g_mqtt_client.ipstack->disconnect != NULL))
    {
        g_mqtt_client.ipstack->disconnect(g_mqtt_client.ipstack);
    }

    g_mqtt_client.mqttstatus = MQTT_START;

    rtos_task_delete(g_mqtt_task_handle);
    g_mqtt_task_handle = NULL;

    RTK_LOGI(TAG, "PC dashboard stopped.\n");
}
