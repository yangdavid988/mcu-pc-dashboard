#include "core/weather.h"
#include "FreeRTOS.h"
#include "basic_types.h"
#include "os_wrapper.h"        /* rtos_time_delay_ms */
#include "log.h"
/* lwIP socket/netdb — only needed when MCU fetches weather via HTTP
   (WEATHER_FETCH_MCU=1).  Guarded because these headers may be unavailable
   when WiFi Kconfig symbols are disabled (e.g. USB CDC mode).           */
#if WEATHER_FETCH_MCU == 1
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip_netconf.h"
#endif
#include "platform_stdlib.h"
#include "config/sdk_compat.h"
#include "task.h"
#include "cJSON.h"
/* ========================================================================
 * OpenWeatherMap Configuration — adjust before building
 *
 * Coordinate-based query (lat/lon) is used instead of city name (q=)
 * for accurate district-level weather in mainland Chinese cities.
 *
 * City display name is auto-detected from the API response's "name" field
 * (derived from coordinates). Set WEATHER_CITY below only if you need to
 * override the auto-detected name (e.g. "Gusu District" vs "Suzhou").
 * ======================================================================== */
// clang-format off
#ifndef WEATHER_API_KEY
#define WEATHER_API_KEY         "YOUR_OPENWEATHERMAP_API_KEY"  /* Get a free key at https://openweathermap.org/api */
#endif

#ifndef WEATHER_CITY
                                        /* Optional: city name override. Empty = use API-returned "name" (auto-detected from coords). */
#define WEATHER_CITY            "Gusu,Jiangsu"
                                        /* ^^ Set e.g. "Gusu District,CN" if API returns an imprecise name like "Suzhou". */
#endif

#ifndef WEATHER_LAT
#define WEATHER_LAT             31.34f                  /* Latitude for coordinate-based query (e.g. Gusu District, Suzhou) */
#endif

#ifndef WEATHER_LON
#define WEATHER_LON             120.61f                 /* Longitude for coordinate-based query */
#endif

#ifndef WEATHER_HOST
#define WEATHER_HOST            "api.openweathermap.org"
#endif

#ifndef WEATHER_PORT
#define WEATHER_PORT            80
#endif

 /** Update interval: 10 minutes (600,000 ms).
  *  Free OpenWeatherMap tier allows 60 calls/minute — 1 per 10min is well within limit. */
#define WEATHER_UPDATE_INTERVAL_MS  600000

 /** Retry interval: 5 seconds on fetch failure (covers transient network issues). */
#define WEATHER_RETRY_INTERVAL_MS   5000

  /** HTTP receive buffer size — must be large enough for full OpenWeatherMap response */
#define HTTP_RECV_BUF_SIZE      4096

/** HTTP request path buffer size */
#define HTTP_PATH_BUF_SIZE      256

#ifndef TAG
#define TAG                     "WEATHER"
#endif

// clang-format on

/* ========================================================================
 * Global data
 * ======================================================================== */
volatile bool  g_weather_updated = false;
Weather_Data_t g_weather         = { 0 };

/* ========================================================================
 * Weather code mapping (compiled in both modes)
 * ======================================================================== */

/** Map OpenWeatherMap condition code (weather[0].id) to main weather group.
 *  https://openweathermap.org/weather-conditions
 *  Returns NULL if code is unrecognised. */
const char* weather_code_to_main(int code)
{
    if (code >= 200 && code < 300) return "Thunderstorm";
    if (code >= 300 && code < 400) return "Drizzle";
    if (code >= 500 && code < 600) return "Rain";
    if (code >= 600 && code < 700) return "Snow";
    if (code >= 700 && code < 800) return "Mist";  /* Atmosphere group */
    if (code == 800)                return "Clear";
    if (code > 800 && code < 810)   return "Clouds";
    return NULL;
}

/* ========================================================================
 * HTTP helpers — only compiled in MCU mode (WEATHER_FETCH_MCU=1)
 * In PC mode, weather data arrives via MQTT.
 * ======================================================================== */

#if WEATHER_FETCH_MCU == 1

/**
 * Build an HTTP 1.1 GET request header string.
 * Returns pointer to static buffer (caller should use before next call).
 */
static const char* http_build_get_header(const char* host, const char* path)
{
    static char header[512];
    int         len = snprintf(header, sizeof(header), "GET %s HTTP/1.1\r\n"
                                                       "Host: %s\r\n"
                                                       "Connection: close\r\n"
                                                       "User-Agent: AmebaRTL8721F/1.0\r\n"
                                                       "\r\n",
                               path,
                               host);
    if (len < 0 || (size_t) len >= sizeof(header))
        header[0] = '\0';
    return header;
}

/**
 * Perform HTTP GET request, return response body (after \r\n\r\n).
 * @param host     Target hostname
 * @param port     Target port
 * @param path     HTTP path (e.g. "/data/2.5/weather?q=...")
 * @param out_buf  Output buffer for response body
 * @param buf_size Output buffer size
 * @return Number of bytes in body (0 = empty body, <0 = error)
 */
static int http_get_body(const char* host, int port, const char* path, char* out_buf, int buf_size)
{
    struct hostent*    server;
    struct sockaddr_in server_addr;
    char*              recv_buf = NULL;
    int                sock     = -1;
    int                ret      = -1;

    if (out_buf == NULL || buf_size <= 0)
        return -1;

    out_buf[0] = '\0';

    /* Create TCP socket */
    sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        RTK_LOGE(TAG, "socket create failed, fd=%d\n", sock);
        return -1;
    }

    recv_buf = (char*) malloc(HTTP_RECV_BUF_SIZE);
    if (recv_buf == NULL)
    {
        RTK_LOGE(TAG, "malloc recv_buf failed\n");
        lwip_close(sock);
        return -1;
    }

    /* Set receive timeout: 10 seconds */
    {
        struct timeval tv;
        tv.tv_sec  = 10;
        tv.tv_usec = 0;
        lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    /* DNS resolve */
    server = lwip_gethostbyname(host);
    if (server == NULL)
    {
        RTK_LOGE(TAG, "DNS resolve failed: %s\n", host);
        goto cleanup;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = lwip_htons((u16_t) port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    /* Connect */
    if (lwip_connect(sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) <
        0)
    {
        RTK_LOGE(TAG, "connect failed: %s:%d\n", host, port);
        goto cleanup;
    }

    /* Send HTTP GET request */
    {
        const char* req     = http_build_get_header(host, path);
        int         req_len = strlen(req);
        if (lwip_write(sock, req, req_len) != req_len)
        {
            RTK_LOGE(TAG, "send request failed\n");
            goto cleanup;
        }
    }

    /* Read response — look for \r\n\r\n header/body boundary */
    {
        int   total = 0;
        int   n;
        char* body_start = NULL;
        int   body_len   = 0;

        while ((n = lwip_read(sock, recv_buf + total, HTTP_RECV_BUF_SIZE - total - 1)) > 0)
        {
            total += n;
            recv_buf[total] = '\0';

            /* Check if we've received the full header */
            if (body_start == NULL)
            {
                body_start = strstr(recv_buf, "\r\n\r\n");
                if (body_start)
                {
                    body_start += 4; /* Skip past \r\n\r\n */
                    body_len = total - (int) (body_start - recv_buf);
                }
            }
            else
            {
                body_len = total - (int) (body_start - recv_buf);
            }

            /* If buffer is almost full, stop */
            if (total >= HTTP_RECV_BUF_SIZE - 1)
                break;
        }

        if (body_start == NULL)
        {
            RTK_LOGE(TAG, "no HTTP body found\n");
            goto cleanup;
        }

        if (body_len <= 0)
        {
            RTK_LOGI(TAG, "empty HTTP body\n");
            out_buf[0] = '\0';
            ret        = 0;
            goto cleanup;
        }

        /* Copy body to output, bounded by buf_size */
        if (body_len >= buf_size)
            body_len = buf_size - 1;
        memcpy(out_buf, body_start, (size_t) body_len);
        out_buf[body_len] = '\0';
        ret               = body_len;
    }

cleanup:
    if (sock >= 0)
        lwip_close(sock);
    if (recv_buf)
        free(recv_buf);
    return ret;
}

/* ========================================================================
 * JSON parser — uses cJSON for OpenWeatherMap current weather response
 *
 * Expected response structure:
 *   {"weather":[{"main":"Clear","description":"clear sky",...}],
 *    "main":{"temp":28.5,"feels_like":26.0,"humidity":60},
 *    "wind":{"speed":3.5},
 *    "name":"Beijing"}
 * ======================================================================== */

/**
 * Parse OpenWeatherMap JSON response into Weather_Data_t (via cJSON).
 * Returns true on success.
 */
static bool parse_weather_json(const char* json, Weather_Data_t* out)
{
    if (json == NULL || out == NULL)
        return false;

    memset(out, 0, sizeof(Weather_Data_t));

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;

    cJSON *item;

    /* --- city name: "name":"Beijing" --- */
    item = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(item) && item->valuestring)
        strncpy(out->city, item->valuestring, sizeof(out->city) - 1);

    /* --- weather array: first element's main + description --- */
    cJSON *weather_arr = cJSON_GetObjectItem(root, "weather");
    if (cJSON_IsArray(weather_arr) && cJSON_GetArraySize(weather_arr) > 0)
    {
        cJSON *weather_obj = cJSON_GetArrayItem(weather_arr, 0);
        if (weather_obj)
        {
            item = cJSON_GetObjectItem(weather_obj, "main");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(out->main, item->valuestring, sizeof(out->main) - 1);

            item = cJSON_GetObjectItem(weather_obj, "description");
            if (cJSON_IsString(item) && item->valuestring)
                strncpy(out->description, item->valuestring, sizeof(out->description) - 1);
        }
    }

    /* --- main object: temp, humidity --- */
    cJSON *main_obj = cJSON_GetObjectItem(root, "main");
    if (cJSON_IsObject(main_obj))
    {
        item = cJSON_GetObjectItem(main_obj, "temp");
        if (cJSON_IsNumber(item))
            out->temp_c = (float) item->valuedouble;

        item = cJSON_GetObjectItem(main_obj, "humidity");
        if (cJSON_IsNumber(item))
            out->humidity = (int) item->valuedouble;
    }

    /* --- wind object: speed --- */
    cJSON *wind_obj = cJSON_GetObjectItem(root, "wind");
    if (cJSON_IsObject(wind_obj))
    {
        item = cJSON_GetObjectItem(wind_obj, "speed");
        if (cJSON_IsNumber(item))
            out->wind_speed = (float) item->valuedouble;
    }

    cJSON_Delete(root);
    out->valid = true;
    return true;
}

/* ========================================================================
 * Weather fetch logic
 * ======================================================================== */

static bool fetch_weather(void)
{
    char           path[HTTP_PATH_BUF_SIZE];
    char*          http_body = NULL;
    Weather_Data_t new_data;

    snprintf(path, sizeof(path), "/data/2.5/weather?lat=%.4f&lon=%.4f&appid=%s&units=metric", (double) WEATHER_LAT, (double) WEATHER_LON, WEATHER_API_KEY);

    RTK_LOGI(TAG, "Fetching weather for (lat=%d.%04d, lon=%d.%04d)%s\n", (int) (WEATHER_LAT), (int) (WEATHER_LAT * 10000) % 10000, (int) (WEATHER_LON), (int) (WEATHER_LON * 10000) % 10000, sizeof(WEATHER_CITY) > 1 ? " — display: " WEATHER_CITY : "");

    http_body = (char*) malloc(HTTP_RECV_BUF_SIZE);
    if (http_body == NULL)
    {
        RTK_LOGE(TAG, "malloc http_body failed\n");
        return false;
    }

    int body_len = http_get_body(WEATHER_HOST, WEATHER_PORT, path, http_body, HTTP_RECV_BUF_SIZE);
    if (body_len <= 0)
    {
        RTK_LOGE(TAG, "HTTP request failed (len=%d)\n", body_len);
        free(http_body);
        return false;
    }

    /* Trim any trailing whitespace/newlines for clean log */
    {
        int i = body_len;
        while (i > 0 && (http_body[i - 1] == '\n' || http_body[i - 1] == '\r' ||
                         http_body[i - 1] == ' '))
            i--;
        http_body[i] = '\0';
    }

    RTK_LOGI(TAG, "HTTP response OK (%d bytes)\n", body_len);

    if (!parse_weather_json(http_body, &new_data))
    {
        RTK_LOGE(TAG, "JSON parse failed\n");
        RTK_LOGI(TAG, "Raw: %s\n", http_body);
        free(http_body);
        return false;
    }

    free(http_body);

    /* Override city with display name only if WEATHER_CITY is explicitly
     * configured (non-empty string). When empty, the API-returned "name" field is
     * kept, which auto-detects the nearest location from coordinates.
     *
     * Compiler optimizes this to a no-op when WEATHER_CITY is "" — sizeof("")
     * == 1. */
    if (sizeof(WEATHER_CITY) > 1)
    {
        strncpy(new_data.city, WEATHER_CITY, sizeof(new_data.city) - 1);
        new_data.city[sizeof(new_data.city) - 1] = '\0';
    }

    /* Update global data */
    taskENTER_CRITICAL();
    memcpy(&g_weather, &new_data, sizeof(Weather_Data_t));
    g_weather.last_update_tick = rtos_time_get_current_system_time_ms();
    g_weather_updated          = true;
    taskEXIT_CRITICAL();

    int w_t10 = (int) (g_weather.temp_c * 10);
    int w_ti  = w_t10 / 10; /* Integer part (negative OK)  */
    int w_tf  = w_t10 % 10; /* Fractional digit (may be negative) */
    if (w_tf < 0)
        w_tf = -w_tf; /* Make fractional part positive */
    RTK_LOGI(TAG, "Weather: %s, %s, %d.%d\xC2\xB0\x43, %d%%\n", g_weather.city, g_weather.description, w_ti, w_tf, g_weather.humidity);
    return true;
}

/* ========================================================================
 * Task entry (MCU mode: HTTP fetch every 10 minutes)
 * ======================================================================== */

void weather_fetch_task(void* param)
{
    (void) param;

    RTK_LOGI(TAG, "Weather task started (MCU mode)\n");

    /* Wait for wifi + DHCP before first fetch */
    while (COMPAT_CHECK_CONNECTIVITY(NETIF_WLAN_STA_INDEX) != CONNECTION_VALID)
    {
        rtos_time_delay_ms(3000);
    }

    RTK_LOGI(TAG, "Wifi connected, starting weather fetch loop\n");

    /* Initial fetch — retry up to 5 times on failure */
    for (int retry = 0; retry < 5; retry++)
    {
        if (fetch_weather())
            break;

        RTK_LOGI(TAG, "Initial fetch failed (attempt %d/5), retrying in %d ms\n", retry + 1, WEATHER_RETRY_INTERVAL_MS);
        rtos_time_delay_ms(WEATHER_RETRY_INTERVAL_MS);
    }

    /* Periodic fetch loop */
    while (1)
    {
        rtos_time_delay_ms(WEATHER_UPDATE_INTERVAL_MS);

        /* If fetch fails, retry up to 5 times before going back to the
         * normal 10-minute cycle */
        if (!fetch_weather())
        {
            RTK_LOGI(TAG, "Periodic fetch failed, retrying...\n");

            for (int retry = 0; retry < 5; retry++)
            {
                rtos_time_delay_ms(WEATHER_RETRY_INTERVAL_MS);

                if (fetch_weather())
                    break;

                RTK_LOGI(TAG, "Retry %d/5 failed, next retry in %d ms\n", retry + 1, WEATHER_RETRY_INTERVAL_MS);
            }
        }
    }

    /* rtos_task_delete(NULL) — unreachable, task runs forever */
}

#else /* WEATHER_FETCH_MCU == 0 — PC mode: weather arrives via MQTT */

/* ========================================================================
 * Task entry (PC mode: no HTTP, just sleep)
 * ======================================================================== */

void weather_fetch_task(void* param)
{
    (void) param;

    RTK_LOGI(TAG, "Weather task started (PC mode — MQTT driven, no HTTP)\n");

    /* Nothing to do — weather data is pushed by PC via MQTT and
     * populated into g_weather by weather_update_data().
     * Task exists only for code symmetry (always created in app_main.c). */
    while (1)
    {
        rtos_time_delay_ms(WEATHER_UPDATE_INTERVAL_MS);
    }
}

/* ========================================================================
 * Weather data updater — called from pc_dashboard.c JSON parser
 * ======================================================================== */

void weather_update_data(float temp_c, int humidity, float wind_speed,
                              const char* description, const char* city,
                              const char* main_group)
{
    Weather_Data_t new_data;
    memset(&new_data, 0, sizeof(new_data));

    /* Keep description whole — do NOT truncate (the old code was
     * truncating "light rain" to "light", losing information). */
    if (description)
        strncpy(new_data.description, description, sizeof(new_data.description) - 1);

    /* Determine main weather group, priority order:
     *   1) Explicit main_group from PC (most reliable)
     *   2) Fallback: derive from condition code (not available here,
     *      handled upstream in pc_dashboard.c which passes main_group)
     *   3) Last-resort: first word of description (legacy, matches
     *      "Clear sky" → "Clear", but fails for "light rain" → "light") */
    if (main_group && main_group[0] != '\0')
    {
        strncpy(new_data.main, main_group, sizeof(new_data.main) - 1);
    }
    else if (description)
    {
        /* Legacy fallback: first word of description.
         * Works for "clear sky" → "Clear" (upper-case C),
         * fails for "light rain" → "light" instead of "Rain". */
        strncpy(new_data.main, description, sizeof(new_data.main) - 1);
        char* space = strchr(new_data.main, ' ');
        if (space)
            *space = '\0';
    }
    new_data.temp_c     = temp_c;
    new_data.humidity   = humidity;
    new_data.wind_speed = wind_speed;
    if (city)
    {
        strncpy(new_data.city, city, sizeof(new_data.city) - 1);
        new_data.city[sizeof(new_data.city) - 1] = '\0';
    }
    /* Override city with display name if WEATHER_CITY is explicitly configured.
     * Same logic as in fetch_weather() — empty WEATHER_CITY keeps the
     * MQTT-provided name. */
    if (sizeof(WEATHER_CITY) > 1)
    {
        strncpy(new_data.city, WEATHER_CITY, sizeof(new_data.city) - 1);
        new_data.city[sizeof(new_data.city) - 1] = '\0';
    }
    new_data.valid            = (description != NULL); /* 0°C is a valid temperature */
    new_data.last_update_tick = rtos_time_get_current_system_time_ms();

    /* Atomic global update */
    taskENTER_CRITICAL();
    memcpy(&g_weather, &new_data, sizeof(Weather_Data_t));
    g_weather_updated = true;
    taskEXIT_CRITICAL();

    int w_t10 = (int) (g_weather.temp_c * 10);
    int w_ti  = w_t10 / 10;
    int w_tf  = w_t10 % 10;
    if (w_tf < 0)
        w_tf = -w_tf;
    RTK_LOGI(TAG, "Weather updated: %s, %d.%d\xC2\xB0\x43, %d%%\n", g_weather.city, w_ti, w_tf, g_weather.humidity);
}

#endif /* WEATHER_FETCH_MCU */
