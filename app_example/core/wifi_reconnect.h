#ifndef _WIFI_RECONNECT_H_
#define _WIFI_RECONNECT_H_
#include "FreeRTOS.h"
#include "basic_types.h"
#include "os_wrapper.h"
#include "platform_stdlib.h"
#include "config/sdk_compat.h"
#include "task.h"
/* lwip_netconf.h / wifi_api.h — unavailable when WiFi Kconfig symbols
   are disabled (USB CDC mode).  Everything below this guard compiles
   only for MQTT / WiFi builds.                                           */
#ifndef CONFIG_USB_CDC_MODE
#include "lwip_netconf.h"
#include "wifi_api.h"
#endif
#include <platform_autoconf.h>
#include "config/threshold_config.h" /* RETRY_LIMIT, RETRY_INTERVAL */

#define SSID           "YOUR_WIFI_SSID" // Replace with your Wi-Fi SSID
#define PASSWORD       "YOUR_WIFI_PASSWORD"

#define LED1_PIN _PA_14 // G
#define LED2_PIN _PA_15 // R

void gpio_led_init(void);
void gpio_toggle(u32 GPIO_Pin, int time_ms);
int  user_wifi_connect(void);
void wifi_connect_task(void);
void wifi_join_status_event_hdl(u8* evt_info);

/* Retry state — shared with MQTT task and UI timer */
extern volatile bool g_wifi_retry_exhausted;
extern volatile int  g_wifi_retry_current;  /* rtw_reconn.cnt */
extern volatile int  g_wifi_retry_max;      /* wifi_user_config.auto_reconnect_count */

/* Called from MQTT park loop / LVGL timer to detect SDK reconnect status */
void wifi_retry_periodic_check(void);

#endif
