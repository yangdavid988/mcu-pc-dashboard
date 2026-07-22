#include "pc_dashboard.h"
#include "sdk_compat.h"
#include <time.h>
#include "threshold_config.h"
/* lwIP SNTP for time sync (fallback when no PC data) */
/* NOTE: SDK lwipopts.h already maps SNTP_UPDATE_DELAY to sntp_get_update_interval(),
   so the interval is controlled at runtime via sntp_set_update_interval(). */
#include "lwip/apps/sntp.h"
   /* Realtek SNTP component (time-of-day thin wrapper, provides set_update_interval) */
#include "sntp/sntp_api.h"

/* ========================================================================
 * Global variables
 * ======================================================================== */
PC_Stats_t      g_pc_stats = { 0 };
volatile bool   g_new_data_ready = false;
volatile bool   g_sht3x_pending = false;
volatile bool   g_mqtt_connected = false;
volatile uint32_t g_data_last_tick = 0;

/* Diagnostic counters */
volatile uint32_t g_mqtt_msg_count = 0;       /* Total MQTT messages received */
volatile uint32_t g_mqtt_last_msg_tick = 0;   /* Tick when last msg was received */

/* Internal globals */
static MQTTClient          g_mqtt_client;
static rtos_task_t         g_mqtt_task_handle = NULL;
static bool                g_sht3x_subscribed = false;   /* Whether SHT3X topic has been subscribed */
static bool                g_pc_event_subscribed = false;  /* Lock screen event subscription */

/* Lock screen state */
volatile ScreenState_t g_screen_state = SCREEN_STATE_MONITOR;
volatile bool          g_lock_screen_active = false;
volatile bool          g_pc_event_received = false;  /* first pc/event retained msg processed */

/* ========================================================================
 * Simple JSON value extractors (flat JSON format)
 * ======================================================================== */

 /*
  * Extract string value: finds "key": "value" pattern
  * Returns number of characters written to out, or -1 if not found
  */
static int json_extract_string(const char* json, const char* key,
    char* out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0)
        return -1;

    char search[64];
    int key_len = snprintf(search, sizeof(search), "\"%s\"", key);
    if (key_len <= 0 || (size_t)key_len >= sizeof(search))
        return -1;

    const char* p_key = strstr(json, search);
    if (!p_key)
        return -1;

    p_key += key_len;

    /* Skip whitespace and colon */
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;
    if (*p_key != ':')
        return -1;
    p_key++;
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;

    /* Expect string starting with " */
    if (*p_key != '"')
        return -1;
    p_key++;

    size_t idx = 0;
    while (*p_key && *p_key != '"' && idx < out_size - 1)
    {
        if (*p_key == '\\' && *(p_key + 1))
        {
            p_key++;
            switch (*p_key)
            {
            case 'n': out[idx++] = '\n'; break;
            case 't': out[idx++] = '\t'; break;
            case 'r': out[idx++] = '\r'; break;
            case '\\': out[idx++] = '\\'; break;
            case '"':  out[idx++] = '"';  break;
            default:   out[idx++] = '\\'; out[idx++] = *p_key; break;
            }
        }
        else
        {
            out[idx++] = *p_key;
        }
        p_key++;
    }
    out[idx] = '\0';
    return (int)idx;
}

/*
 * Extract numeric value: finds "key": number
 * Returns true on success, false on failure
 */
static bool json_extract_number(const char* json, const char* key,
    float* value)
{
    if (!json || !key || !value)
        return false;

    char search[64];
    int key_len = snprintf(search, sizeof(search), "\"%s\"", key);
    if (key_len <= 0 || (size_t)key_len >= sizeof(search))
        return false;

    const char* p_key = strstr(json, search);
    if (!p_key)
        return false;

    p_key += key_len;

    /* Skip whitespace and colon */
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;
    if (*p_key != ':')
        return false;
    p_key++;
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;

    /* Check for null or string */
    if (strncmp(p_key, "null", 4) == 0)
        return false;
    if (*p_key == '"')
        return false;   /* Value is a string, not a number */

    char num_buf[64];
    size_t idx = 0;
    while (*p_key && idx < sizeof(num_buf) - 1)
    {
        if (*p_key == ',' || *p_key == '}' || *p_key == ' ' || *p_key == '\t' ||
            *p_key == '\n' || *p_key == '\r')
            break;
        num_buf[idx++] = *p_key++;
    }
    num_buf[idx] = '\0';

    if (idx == 0)
        return false;

    *value = (float)atof(num_buf);
    return true;
}

/*
 * Extract integer: finds "key": integer
 * Returns true on success, false on failure
 */
static bool json_extract_int(const char* json, const char* key,
    int32_t* value)
{
    float fval;
    if (!json_extract_number(json, key, &fval))
        return false;
    *value = (int32_t)fval;
    return true;
}

/*
 * Extract boolean: finds "key": true/false
 * Returns true on success, false on failure
 */
static bool json_extract_bool(const char* json, const char* key,
    bool* value)
{
    if (!json || !key || !value)
        return false;

    char search[64];
    int key_len = snprintf(search, sizeof(search), "\"%s\"", key);
    if (key_len <= 0 || (size_t)key_len >= sizeof(search))
        return false;

    const char* p_key = strstr(json, search);
    if (!p_key)
        return false;

    p_key += key_len;

    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;
    if (*p_key != ':')
        return false;
    p_key++;
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;

    if (strncmp(p_key, "true", 4) == 0)
    {
        *value = true;
        return true;
    }
    else if (strncmp(p_key, "false", 5) == 0)
    {
        *value = false;
        return true;
    }
    return false;
}

/*
 * Extract uint64_t: finds "key": big_number
 * Uses strtoull directly to avoid float precision loss (>2^53 cannot be represented exactly as float)
 */
static bool json_extract_u64(const char* json, const char* key,
    uint64_t* value)
{
    if (!json || !key || !value)
        return false;

    char search[64];
    int key_len = snprintf(search, sizeof(search), "\"%s\"", key);
    if (key_len <= 0 || (size_t)key_len >= sizeof(search))
        return false;

    const char* p_key = strstr(json, search);
    if (!p_key)
        return false;

    p_key += key_len;

    /* Skip whitespace and colon */
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;
    if (*p_key != ':')
        return false;
    p_key++;
    while (*p_key && (*p_key == ' ' || *p_key == '\t' || *p_key == '\n' || *p_key == '\r'))
        p_key++;

    /* Check for null or string */
    if (strncmp(p_key, "null", 4) == 0)
        return false;
    if (*p_key == '"')
        return false;

    /* Parse directly with strtoull to avoid float precision loss */
    char* endptr = NULL;
    *value = strtoull(p_key, &endptr, 10);
    return (endptr != p_key);
}

/* ========================================================================
 * Unix timestamp to date/time conversion
 * Algorithm: Gregorian calendar (1970 epoch)
 * ======================================================================== */
void unix_to_datetime(uint32_t timestamp,
    uint16_t* year, uint8_t* month, uint8_t* day,
    uint8_t* hour, uint8_t* min, uint8_t* sec)
{
    static const uint8_t days_in_months[12] =
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    static const uint16_t days_in_year[2] = { 365, 366 }; /* 0=common year, 1=leap year */

    uint32_t t = timestamp;

    /* Hours, minutes, seconds */
    if (sec)   *sec = (uint8_t)(t % 60); t /= 60;
    if (min)   *min = (uint8_t)(t % 60); t /= 60;
    if (hour)  *hour = (uint8_t)(t % 24); t /= 24;

    /* Days since 1970-01-01 */
    uint32_t days = t;

    uint16_t y = 1970;
    while (1)
    {
        int is_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        uint16_t dpy = days_in_year[is_leap ? 1 : 0];
        if (days < dpy)
            break;
        days -= dpy;
        y++;
    }

    if (year) *year = y;

    uint8_t m;
    for (m = 0; m < 12; m++)
    {
        int is_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        uint8_t dim = days_in_months[m];
        if (m == 1 && is_leap)
            dim = 29;
        if (days < dim)
            break;
        days -= dim;
    }
    if (month) *month = m + 1;
    if (day)   *day = (uint8_t)(days + 1);
}

/* ========================================================================
 * Byte size formatter
 * ======================================================================== */
void format_bytes(uint64_t bytes, char* out, size_t out_size)
{
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unit_idx = 0;
    double val = (double)bytes;

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
 * JSON parser: parse PC stats JSON data into g_pc_stats
 * ======================================================================== */
static void parse_pc_stats_json(const char* payload)
{
    PC_Stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.has_data = false;

    /* Parse numeric fields */
    float fval;
    int32_t ival;
    bool bval;

    if (json_extract_number(payload, "cpu", &fval))
        stats.cpu = fval;
    if (json_extract_number(payload, "mem", &fval))
        stats.mem = fval;
    if (json_extract_u64(payload, "mem_total", &stats.mem_total) == false)
        stats.mem_total = 0;
    if (json_extract_u64(payload, "mem_used", &stats.mem_used) == false)
        stats.mem_used = 0;
    if (json_extract_number(payload, "disk", &fval))
        stats.disk = fval;
    if (json_extract_number(payload, "net_upload_kbps", &fval))
        stats.net_upload_kbps = fval;
    if (json_extract_number(payload, "net_download_kbps", &fval))
        stats.net_download_kbps = fval;

    /* CPU temperature: may be null */
    stats.cpu_temp_valid = json_extract_number(payload, "cpu_temp", &fval);
    if (stats.cpu_temp_valid)
        stats.cpu_temp = fval;
    else
        stats.cpu_temp = 0.0f;

    /* Parse boot_time via strtoull to avoid float32 precision loss */
    {
        uint64_t bt_u64 = 0;
        if (json_extract_u64(payload, "boot_time", &bt_u64))
            stats.boot_time = (uint32_t)bt_u64;
    }
    if (json_extract_int(payload, "process_count", &ival))
        stats.process_count = (uint32_t)ival;
    if (json_extract_int(payload, "cpu_cores_logical", &ival))
        stats.cpu_cores_logical = (uint8_t)ival;
    if (json_extract_int(payload, "cpu_cores_physical", &ival))
        stats.cpu_cores_physical = (uint8_t)ival;
    /* Parse timestamp via strtoull to avoid float32 precision loss (2026 timestamps exceed 2^24 mantissa) */
    {
        uint64_t ts_u64 = 0;
        if (json_extract_u64(payload, "timestamp", &ts_u64))
            stats.timestamp = (uint32_t)ts_u64;
    }

    if (json_extract_number(payload, "battery_percent", &fval))
        stats.battery_percent = fval;
    if (json_extract_bool(payload, "battery_plugged", &bval))
        stats.battery_plugged = bval;

    if (json_extract_u64(payload, "disk_read_bytes", &stats.disk_read_bytes) == false)
        stats.disk_read_bytes = 0;
    if (json_extract_u64(payload, "disk_write_bytes", &stats.disk_write_bytes) == false)
        stats.disk_write_bytes = 0;

    /* Username */
    char user_buf[32] = { 0 };
    if (json_extract_string(payload, "current_user", user_buf, sizeof(user_buf)) >= 0)
    {
        strncpy(stats.current_user, user_buf, sizeof(stats.current_user) - 1);
        stats.current_user[sizeof(stats.current_user) - 1] = '\0';
    }

    /* ===== V2 field parsers ===== */

    /* CPU frequency (negative = unavailable) */
    if (json_extract_number(payload, "cpu_freq_current", &fval))
        stats.cpu_freq_current = fval;
    else
        stats.cpu_freq_current = -1.0f;
    if (json_extract_number(payload, "cpu_freq_min", &fval))
        stats.cpu_freq_min = fval;
    else
        stats.cpu_freq_min = -1.0f;
    if (json_extract_number(payload, "cpu_freq_max", &fval))
        stats.cpu_freq_max = fval;
    else
        stats.cpu_freq_max = -1.0f;

    /* Hostname */
    char hostname_buf[64] = { 0 };
    if (json_extract_string(payload, "hostname", hostname_buf, sizeof(hostname_buf)) >= 0)
    {
        strncpy(stats.hostname, hostname_buf, sizeof(stats.hostname) - 1);
        stats.hostname[sizeof(stats.hostname) - 1] = '\0';
    }

    /* OS platform */
    char os_buf[64] = { 0 };
    if (json_extract_string(payload, "os_platform", os_buf, sizeof(os_buf)) >= 0)
    {
        strncpy(stats.os_platform, os_buf, sizeof(stats.os_platform) - 1);
        stats.os_platform[sizeof(stats.os_platform) - 1] = '\0';
    }

    /* Swap */
    if (json_extract_number(payload, "swap_percent", &fval))
        stats.swap_percent = fval;
    if (json_extract_u64(payload, "swap_total", &stats.swap_total) == false)
        stats.swap_total = 0;
    if (json_extract_u64(payload, "swap_used", &stats.swap_used) == false)
        stats.swap_used = 0;

    /* GPU info (negative = unavailable) */
    char gpu_name_buf[64] = { 0 };
    if (json_extract_string(payload, "gpu_name", gpu_name_buf, sizeof(gpu_name_buf)) >= 0)
    {
        strncpy(stats.gpu_name, gpu_name_buf, sizeof(stats.gpu_name) - 1);
        stats.gpu_name[sizeof(stats.gpu_name) - 1] = '\0';
    }
    if (json_extract_number(payload, "gpu_usage", &fval))
        stats.gpu_usage = fval;
    else
        stats.gpu_usage = -1.0f;
    if (json_extract_number(payload, "gpu_mem_used_mb", &fval))
        stats.gpu_mem_used_mb = fval;
    else
        stats.gpu_mem_used_mb = -1.0f;
    if (json_extract_number(payload, "gpu_mem_total_mb", &fval))
        stats.gpu_mem_total_mb = fval;
    else
        stats.gpu_mem_total_mb = -1.0f;
    if (json_extract_number(payload, "gpu_temp_c", &fval))
        stats.gpu_temp_c = fval;
    else
        stats.gpu_temp_c = -1.0f;

    /* Disk I/O utilization */
    if (json_extract_number(payload, "disk_io_percent", &fval))
        stats.disk_io_percent = fval;
    else
        stats.disk_io_percent = -1.0f;

    stats.has_data = true;

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
    char event_buf[16] = { 0 };
    if (json_extract_string(payload, "event", event_buf, sizeof(event_buf)) < 0)
    {
        return;
    }

    if (strcmp(event_buf, "lock") == 0)
    {
        taskENTER_CRITICAL();
        g_screen_state = SCREEN_STATE_CLOCK;
        g_pc_event_received = true;
        taskEXIT_CRITICAL();
        RTK_LOGI(TAG, "Lock event received -> CLOCK mode\n");
    }
    else if (strcmp(event_buf, "unlock") == 0)
    {
        taskENTER_CRITICAL();
        g_screen_state = SCREEN_STATE_MONITOR;
        g_pc_event_received = true;
        g_new_data_ready = true;
        taskEXIT_CRITICAL();
        RTK_LOGI(TAG, "Unlock event received -> MONITOR mode\n");
    }
}

/* ========================================================================
 * SHT3X JSON parser
 * ======================================================================== */
static void parse_sht3x_json(const char* payload)
{
    float temp_val_c = 0.0f, temp_val_f = 0.0f, humi_val = 0.0f;
    bool  temp_ok = false, temp_f_ok = false, humi_ok = false;

    if (json_extract_number(payload, "temperature_C", &temp_val_c))
    {
        temp_ok = true;
    }

    if (json_extract_number(payload, "temperature_F", &temp_val_f))
    {
        temp_f_ok = true;
    }

    if (json_extract_number(payload, "humidity", &humi_val))
    {
        humi_ok = true;
    }

    if (!temp_ok && !humi_ok)
    {
        return;
    }

    taskENTER_CRITICAL();
    /* Read current values for threshold comparison */
    float cur_temp = g_pc_stats.sht3x_temperature;
    float cur_humi = g_pc_stats.sht3x_humidity;
    bool had_data = g_pc_stats.sht3x_valid;

    /* Always update storage with latest values */
    if (temp_ok)
        g_pc_stats.sht3x_temperature = temp_val_c;
    if (temp_f_ok)
        g_pc_stats.sht3x_temperature_f = temp_val_f;
    if (humi_ok)
        g_pc_stats.sht3x_humidity = humi_val;
    g_pc_stats.sht3x_valid = true;
    g_pc_stats.has_data = true;

    /* Only trigger a UI refresh if change exceeds threshold — tiny
     * fluctuations (e.g. ±0.1°C) are discarded to avoid unnecessary
     * data refreshes that contribute to multi-rect tearing.         */
    {
        float d_temp = (temp_ok) ? ((temp_val_c > cur_temp) ? (temp_val_c - cur_temp) : (cur_temp - temp_val_c)) : 0.0f;
        float d_humi = (humi_ok) ? ((humi_val > cur_humi) ? (humi_val - cur_humi) : (cur_humi - humi_val)) : 0.0f;
        bool above_threshold = !had_data ||
            d_temp >= SHT3X_THRESHOLD_TEMP_C ||
            d_humi >= SHT3X_THRESHOLD_HUMI_PCT;
        if (above_threshold)
        {
            g_sht3x_pending = true;    /* Don't set g_new_data_ready — wait for next JSON refresh sync */
        }
    }
    taskEXIT_CRITICAL();
}

/* ========================================================================
 * MQTT message arrival callback (routes by topic)
 * ======================================================================== */
static void messageArrived(MessageData* data, void* discard)
{
    (void)discard;

    char* topic = data->topicName->lenstring.data;
    int   topic_len = data->topicName->lenstring.len;
    char* payload = (char*)data->message->payload;
    int   payload_len = data->message->payloadlen;

    /* Copy payload as C string for parsing.
     * Static buffer: 2048 bytes on stack would strain MQTT task (8192B stack).
     * Reuse same buffer across invocations since this runs in MQTT task context only. */
    static char json_buf[JSON_PARSE_BUF_SIZE];
    int copy_len = payload_len;
    if (copy_len >= (int)sizeof(json_buf))
    {
        copy_len = sizeof(json_buf) - 1;
        mqtt_printf(MQTT_INFO,
            "DIAG: msg truncated! payload_len=%d buf=%u\n",
            payload_len, (unsigned int)sizeof(json_buf));
    }
    memcpy(json_buf, payload, copy_len);
    json_buf[copy_len] = '\0';

    /* Route by topic */
    if (topic_len == (int)strlen(MQTT_TOPIC_PC_STATS) &&
        strncmp(topic, MQTT_TOPIC_PC_STATS, topic_len) == 0)
    {
        parse_pc_stats_json(json_buf);
    }
    else if (topic_len == (int)strlen(MQTT_TOPIC_SHT3X) &&
        strncmp(topic, MQTT_TOPIC_SHT3X, topic_len) == 0)
    {
        parse_sht3x_json(json_buf);
    }
    else if (topic_len == (int)strlen(MQTT_TOPIC_PC_EVENT) &&
        strncmp(topic, MQTT_TOPIC_PC_EVENT, topic_len) == 0)
    {
        parse_lock_event(json_buf);
    }
    else
    {
        /* Silently ignore unmatched topics */
    }

    /* Diagnostics: update message counter (called from MQTT task context) */
    g_mqtt_msg_count++;
    g_mqtt_last_msg_tick = rtos_time_get_current_system_time_ms();
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
    empty.cpu_freq_min = -1.0f;
    empty.cpu_freq_max = -1.0f;
    empty.gpu_usage = -1.0f;
    empty.gpu_mem_used_mb = -1.0f;
    empty.gpu_mem_total_mb = -1.0f;
    empty.gpu_temp_c = -1.0f;
    empty.disk_io_percent = -1.0f;
    empty.cpu_temp_valid = false;
    empty.sht3x_valid = false;
    empty.has_data = false;
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
    (void)parameters;

    Network                network;
    unsigned char          sendbuf[MQTT_SENDBUF_SIZE];
    unsigned char          readbuf[MQTT_READBUF_SIZE];
    int                    rc = 0;
    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
    const char* address = MQTT_BROKER_ADDRESS;

    /* MQTT connection params: subscribe only, no Will message */
    connectData.MQTTVersion = 3;
    connectData.clientID.cstring = (char*)MQTT_CLIENT_ID;
    connectData.willFlag = 0;

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
    sntp_set_update_interval(86400000);  /* re-sync every 24h (Realtek wrapper) */
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_init();
    RTK_LOGI(TAG, "SNTP initialized (server: ntp.aliyun.com)\n");

    /* Network / MQTT client initialization */
    NetworkInit(&network);
    network.use_ssl = 1;
    connectData.username.cstring = (char*)MQTT_USERNAME;
    connectData.password.cstring = (char*)MQTT_PASSWORD;

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
        fd_set        read_fds;
        fd_set        except_fds;
        struct timeval timeout;

        FD_ZERO(&read_fds);
        FD_ZERO(&except_fds);

        timeout.tv_sec = MQTT_SELECT_TIMEOUT;
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
                "FreeRTOS_Select failed, rc=%d\n", rc);
        }

        if (FD_ISSET(network.my_socket, &except_fds))
        {
            mqtt_printf(MQTT_INFO, "except_fds set, reconnecting...\n");
            MQTTSetStatus(&g_mqtt_client, MQTT_START);
        }

        /* MQTT alive heartbeat (~1 per 5s) */
        {
            static uint32_t s_diag_mqtt_tick = 0;
            static uint32_t s_diag_mqtt_cnt = 0;
            uint32_t _now = rtos_time_get_current_system_time_ms();
            if (_now - s_diag_mqtt_tick >= 5000)
            {
                s_diag_mqtt_tick = _now;
#if defined(CONFIG_DIAG_HEARTBEAT)
                RTK_LOGI(TAG, "DIAG: mqtt cnt=%lu status=%d\n",
                    (unsigned long)s_diag_mqtt_cnt,
                    (int)g_mqtt_client.mqttstatus);
#endif
            }
            s_diag_mqtt_cnt++;
        }

        /* MQTT state machine (connect, subscribe, receive) */
        MQTTDataHandle(&g_mqtt_client,
            &read_fds,
            &connectData,
            messageArrived,
            (char*)address,
            (char*)MQTT_SUB_TOPIC);

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
            RTK_LOGI(TAG, "Subscribing to SHT3X topic: %s\n",
                MQTT_SUB_TOPIC_SHT3X);
            int sub_rc = MQTTSubscribe(&g_mqtt_client,
                MQTT_SUB_TOPIC_SHT3X,
                QOS0,
                messageArrived);

            /* MQTT_TASK mode: MQTTSubscribe() doesn't register handler, do it manually */
            if (sub_rc == 0)
            {
                int i;
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
            g_sht3x_subscribed = false;  /* Reset on disconnect for re-subscribe on reconnect */
        }

        /* Subscribe to lock screen event topic */
        if (g_mqtt_client.mqttstatus == MQTT_RUNNING && !g_pc_event_subscribed)
        {
            RTK_LOGI(TAG, "Subscribing to lock event topic: %s\n",
                MQTT_SUB_TOPIC_EVENT);
            int sub_rc = MQTTSubscribe(&g_mqtt_client,
                MQTT_SUB_TOPIC_EVENT,
                QOS0,
                messageArrived);

            if (sub_rc == 0)
            {
                int i;
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
