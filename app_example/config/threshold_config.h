// threshold_config.h — Flash Warning Threshold Configuration
// Adjust these values for your hardware/environment before building.
#pragma once
#include <stdint.h>

typedef struct
{
    float cpu_pct;           // CPU usage threshold (%)            default: 80.0
    float cpu_temp_c;        // CPU temperature threshold (°C)     default: 70.0
    float ram_pct;           // RAM usage threshold (%)            default: 80.0
    float disk_pct;          // Disk usage threshold (%)           default: 90.0
    float gpu_pct;           // GPU usage threshold (%)            default: 80.0
    float bat_low_pct;       // Battery low threshold (%)          default: 20.0
    float env_temp_c;        // Environmental temperature (°C)     default: 40.0
    int   flash_interval_ms; // Flash toggle interval (ms)         default: 150
} flash_threshold_t;

/* ========================================================================
 * SHT3X sensor update threshold — ignore tiny fluctuations
 * ======================================================================== */

/** Temperature change threshold (°C) — skip refresh if delta is smaller */
#define SHT3X_THRESHOLD_TEMP_C   0.5f /* default: ±0.5°C */

/** Humidity change threshold (%RH) — skip refresh if delta is smaller */
#define SHT3X_THRESHOLD_HUMI_PCT 5.0f /* default: ±5 %RH */

/* ========================================================================
 * UI timing
 * ======================================================================== */

/** LVGL timer interval (ms).  Clock ticks every second; data widgets
 *  refresh only on new JSON arrival (driven by g_new_data_ready).        */
#define UI_UPDATE_INTERVAL_MS       1000

/* ========================================================================
 * Connection data freshness timeout
 *
 * When the MCU stops receiving PC data (TCP socket alive but Python
 * script died, or USB cable plugged in but sender exited), this timeout
 * detects the stall and resets displayed values to defaults.
 *
 * A disconnect event {"event":"disconnect"} from the PC script overrides
 * the timeout and triggers an immediate disconnection.
 * ======================================================================== */

#define CONNECTION_TIMEOUT_MS       12000  /* ms, ~4× the 3s MQTT publish interval */

/* ========================================================================
 * Backlight brightness control
 * ======================================================================== */

/** Master enable: 0 = always 100 %, 1 = allow dimming in standby */
#define BRIGHTNESS_ENABLED 1

/** Standby brightness (0..100).  Panel-specific due to different remap curves. */
#ifdef CONFIG_SCREEN_DBL070
#define BRIGHTNESS_STANDBY_PCT 20   /* cubic: 20% → 0.8% duty → dimly visible */
#elif defined(CONFIG_SCREEN_ST7262)
#define BRIGHTNESS_STANDBY_PCT 3    /* quadratic: 3% → 0.09% duty */
#endif

/** Normal brightness (0..100).  100 = full brightness in monitor UI */
#define BRIGHTNESS_NORMAL_PCT 100

/** Lowest allowed brightness (%) — hard floor. */
#ifdef CONFIG_SCREEN_DBL070
#define BL_MIN_PCT  10
#elif defined(CONFIG_SCREEN_ST7262)
#define BL_MIN_PCT  5
#endif

/** Step size (%) for backlight_adjust(+/-) */
#define BL_STEP_PCT 10

/* ========================================================================
 * WiFi reconnect retry
 * ======================================================================== */

/** Max consecutive Wi-Fi reconnection attempts before giving up */
#define RETRY_LIMIT    10

/** Delay between reconnection attempts (ms).  Combined with RETRY_LIMIT
 *  gives a max of ~50 s before the MCU stops retrying.                  */
#define RETRY_INTERVAL 5000

#ifndef FLASH_THRESHOLD_EXTERNAL
static const flash_threshold_t g_flash_threshold = {
    .cpu_pct           = 80.0f,
    .cpu_temp_c        = 80.0f,
    .ram_pct           = 80.0f,
    .disk_pct          = 90.0f,
    .gpu_pct           = 80.0f,
    .bat_low_pct       = 20.0f,
    .env_temp_c        = 35.0f,
    .flash_interval_ms = 150,
};
#else
extern const flash_threshold_t g_flash_threshold;
#endif
