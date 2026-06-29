#ifndef _PC_DASHBOARD_H_
#define _PC_DASHBOARD_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ameba_soc.h"
#include "platform_stdlib.h"
#include "basic_types.h"
#include "os_wrapper.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "MQTTClient.h"
#include "gpio_api.h"
#include "device.h"
#include "lvgl.h"
#include "lv_timer.h"
#include "drv/lcd/lcd_drv.h"

/* ========================================================================
 * Project Identity
 * ======================================================================== */
#define TAG                     "PC_DASHBOARD"

 /* ========================================================================
  * RGB LCD Configuration (800x480)
  * ======================================================================== */
#define SCREEN_WIDTH            800
#define SCREEN_HEIGHT           480
#define LVGL_BUF_SIZE           (SCREEN_WIDTH * SCREEN_HEIGHT * 4)  /* ARGB8888: 4 bytes/pixel */

  /* ========================================================================
 * MQTT Configuration
 * ======================================================================== */
#define MQTT_SELECT_TIMEOUT         1
#ifndef MQTT_SENDBUF_SIZE
#define MQTT_SENDBUF_SIZE           256
#endif
#ifndef MQTT_READBUF_SIZE
#define MQTT_READBUF_SIZE           1536
#endif
#ifndef JSON_PARSE_BUF_SIZE
#define JSON_PARSE_BUF_SIZE         2048    /* Increased buffer for larger JSON payloads (long GPU names, hostname) */
#endif
#define MQTT_TOPIC_PC_STATS         "pc/stats"
#define MQTT_TOPIC_SHT3X            "humiture/measurement"
#define MQTT_SUB_TOPIC              "pc/stats"      /* Subscribe to PC stats topic */
#define MQTT_SUB_TOPIC_SHT3X       "humiture/measurement"  /* SHT3X temperature/humidity topic */
#define MQTT_BROKER_ADDRESS         "YOUR_BROKER.emqxsl.cn"
#define MQTT_BROKER_PORT            8883
#define MQTT_CLIENT_ID              "PC_DASHBOARD_001"
#define MQTT_USERNAME               "YOUR_MQTT_USERNAME"
#define MQTT_PASSWORD               "YOUR_MQTT_PASSWORD"

 /* ========================================================================
  * UI Update Interval (ms)
  * ======================================================================== */
#define UI_UPDATE_INTERVAL_MS       1000             /* Clock updates every second, data refreshes only on new arrival */
#define UTC8_OFFSET_SEC             28800            /* UTC+8 offset (8 hours) */

  /* ========================================================================
   * PC Status Data Structure
   * ======================================================================== */
typedef struct
{
  /* Resource usage */
  float       cpu;                    /* CPU usage (%) */
  float       mem;                    /* Memory usage (%) */
  uint64_t    mem_total;              /* Physical memory total (bytes) */
  uint64_t    mem_used;               /* Physical memory used (bytes) */
  float       disk;                   /* Disk usage (%) */

  /* Network rate (KB/s) */
  float       net_upload_kbps;
  float       net_download_kbps;

  /* Sensors */
  float       cpu_temp;               /* CPU temperature (°C), NAN if unavailable */
  bool        cpu_temp_valid;

  /* System info */
  uint32_t    boot_time;              /* Boot time (Unix timestamp) */
  uint32_t    process_count;          /* Process count */
  uint8_t     cpu_cores_logical;      /* Logical core count */
  uint8_t     cpu_cores_physical;     /* Physical core count */
  char        current_user[32];       /* Current username */

  /* Battery */
  float       battery_percent;        /* Battery percentage */
  bool        battery_plugged;        /* Whether AC power is connected */

  /* Disk I/O */
  uint64_t    disk_read_bytes;
  uint64_t    disk_write_bytes;

  /* Timestamp */
  uint32_t    timestamp;              /* Data collection time (Unix timestamp) */

  /* SHT3X temperature/humidity (from MQTT topic humiture/measurement) */
  float       sht3x_temperature;      /* SHT3X temperature (°C) */
  float       sht3x_temperature_f;    /* SHT3X temperature (°F) */
  float       sht3x_humidity;         /* SHT3X humidity (%) */
  bool        sht3x_valid;            /* Whether SHT3X data is valid */

  /* ===== V2 added fields ===== */

  /* CPU frequency (MHz) */
  float       cpu_freq_current;       /* Current frequency, <0 means unavailable */
  float       cpu_freq_min;           /* Min frequency, <0 means unavailable */
  float       cpu_freq_max;           /* Max frequency, <0 means unavailable */

  /* Host / OS */
  char        hostname[64];           /* Hostname */
  char        os_platform[64];        /* OS platform version */

  /* Swap */
  uint64_t    swap_total;             /* Swap total bytes */
  uint64_t    swap_used;              /* Swap used bytes */
  float       swap_percent;           /* Swap usage (%) */

  /* GPU */
  char        gpu_name[64];           /* GPU model */
  float       gpu_usage;              /* GPU usage (%), <0 means unavailable */
  float       gpu_mem_used_mb;        /* GPU memory used (MB), <0 means unavailable */
  float       gpu_mem_total_mb;       /* GPU memory total (MB), <0 means unavailable */
  float       gpu_temp_c;             /* GPU temperature (°C), <0 means unavailable */

  /* Disk I/O utilization */
  float       disk_io_percent;        /* Disk I/O utilization (%), <0 means unavailable */

  /* Status flags */
  bool        has_data;               /* Whether valid data has been received */
} PC_Stats_t;

/* ========================================================================
 * Global variable declarations (defined in pc_dashboard.c)
 * ======================================================================== */
extern PC_Stats_t       g_pc_stats;
extern volatile bool    g_new_data_ready;
extern volatile bool    g_mqtt_connected;           /* MQTT connection status */
extern volatile uint32_t g_data_last_tick;          /* System tick (ms) when last data was received */

/* Reset g_pc_stats to defaults (called on MQTT disconnect) */
void pc_stats_reset_to_default(void);

/* ========================================================================
 * Clock time base (set by pc_dashboard_ui.c, used by V3 layout update functions)
 * ======================================================================== */
extern uint32_t         g_time_base_ts;
extern uint32_t         g_time_base_ms;

/* ========================================================================
 * Function declarations
 * ======================================================================== */

 /* Main task function */
void pc_dashboard_task(void* parameters);

/* Start/stop interface */
void pc_dashboard_start(void);
void pc_dashboard_stop(void);

/* Timestamp conversion */
void unix_to_datetime(uint32_t timestamp,
  uint16_t* year, uint8_t* month, uint8_t* day,
  uint8_t* hour, uint8_t* min, uint8_t* sec);

/* Byte size formatter (B/KB/MB/GB/TB) */
void format_bytes(uint64_t bytes, char* out, size_t out_size);

#endif /* _PC_DASHBOARD_H_ */
