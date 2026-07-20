// threshold_config.h — Flash Warning Threshold Configuration
// Adjust these values for your hardware/environment before building.
#pragma once
#include <stdint.h>

typedef struct {
    float cpu_pct;           // CPU usage threshold (%)            default: 80.0
    float cpu_temp_c;        // CPU temperature threshold (°C)     default: 70.0
    float ram_pct;           // RAM usage threshold (%)            default: 80.0
    float disk_pct;          // Disk usage threshold (%)           default: 90.0
    float gpu_pct;           // GPU usage threshold (%)            default: 80.0
    float bat_low_pct;       // Battery low threshold (%)          default: 20.0
    float env_temp_c;        // Environmental temperature (°C)     default: 40.0
    int   flash_interval_ms; // Flash toggle interval (ms)         default: 150
} flash_threshold_t;

/* SHT3X update threshold — ignore tiny fluctuations to avoid unnecessary refreshes */
#define SHT3X_THRESHOLD_TEMP_C     0.5f   /* °C, default: ±0.5°C */
#define SHT3X_THRESHOLD_HUMI_PCT   5.0f   /* %RH, default: ±5% */

#ifndef FLASH_THRESHOLD_EXTERNAL
static const flash_threshold_t g_flash_threshold = {
    .cpu_pct = 80.0f,
    .cpu_temp_c = 80.0f,
    .ram_pct = 80.0f,
    .disk_pct = 90.0f,
    .gpu_pct = 80.0f,
    .bat_low_pct = 20.0f,
    .env_temp_c = 35.0f,
    .flash_interval_ms = 150,
};
#else
extern const flash_threshold_t g_flash_threshold;
#endif
