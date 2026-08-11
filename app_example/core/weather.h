#ifndef _WEATHER_H_
#define _WEATHER_H_

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Weather Source Selection
 *
 * WEATHER_FETCH_MCU:
 *   0 = PC pushes weather via MQTT (default). MCU does no HTTP fetch.
 *       Weather fields are parsed from pc/stats JSON in pc_dashboard.c.
 *   1 = MCU fetches weather directly from OpenWeatherMap via HTTP.
 *       Requires WEATHER_API_KEY + WEATHER_CITY configured in weather.c.
 *
 * IMPORTANT: WEATHER_FETCH_MCU must be consistent between weather.c and
 * pc_dashboard.c — define it once here or override in your build system.
 * ======================================================================== */
#ifndef WEATHER_FETCH_MCU
#define WEATHER_FETCH_MCU 1 /* 0 = PC pushes via MQTT, 1 = MCU fetches HTTP */
#endif

/* ========================================================================
 * Weather Data — fetched from OpenWeatherMap API via HTTP (MCU mode)
 *            or  parsed from MQTT pc/stats JSON (PC mode)
 * ======================================================================== */

#define WEATHER_CITY_MAX_LEN 32
#define WEATHER_DESC_MAX_LEN 64

/** Weather fetch result */
typedef struct
{
    char     city[WEATHER_CITY_MAX_LEN];        /* City name (e.g. "Beijing") */
    char     description[WEATHER_DESC_MAX_LEN]; /* Weather description (e.g. "clear sky") */
    char     main[WEATHER_DESC_MAX_LEN];        /* Weather group (e.g. "Clear", "Clouds", "Rain") */
    float    temp_c;                            /* Current temperature (°C) */
    int      humidity;                          /* Humidity (%) */
    float    wind_speed;                        /* Wind speed (m/s) */
    bool     valid;                             /* Whether valid data has been fetched at least once */
    uint32_t last_update_tick;                  /* System tick (ms) of last successful fetch */
} Weather_Data_t;

/* ========================================================================
 * Globals
 * ======================================================================== */
extern volatile bool  g_weather_updated; /* Set to true when new weather data arrives */
extern Weather_Data_t g_weather;         /* Latest weather data (read-only from UI) */

/* ========================================================================
 * API
 * ======================================================================== */

/** Weather fetch task entry — creates a periodic task that fetches weather
 *  every 10 minutes. Waits for wifi connectivity before the first fetch.
 *
 *  In MCU mode (WEATHER_FETCH_MCU=1): performs HTTP GET to OpenWeatherMap.
 *  In PC  mode (WEATHER_FETCH_MCU=0): task sleeps forever; weather data
 *  arrives via MQTT and is pushed into g_weather by weather_update_from_mqtt(). */
void weather_fetch_task(void* param);

#if WEATHER_FETCH_MCU == 0
/** Update weather data from PC-pushed MQTT fields.
 *  Called by parse_pc_stats_json() in pc_dashboard.c when weather_*
 *  fields are present in the MQTT payload.
 *  Sets g_weather_updated = true so the UI timer refreshes weather display. */
void weather_update_from_mqtt(float temp_c, int humidity, float wind_speed,
                              const char* description, const char* city,
                              const char* main_group);
#endif

/** Convert OpenWeatherMap condition code (weather[0].id) to main weather
 *  group string, or NULL if unrecognised.
 *  https://openweathermap.org/weather-conditions
 *  Example: 500 → "Rain", 800 → "Clear", 802 → "Clouds" */
const char* weather_code_to_main(int condition_code);

#endif /* _WEATHER_H_ */
