#ifndef _WIFI_RECONNECT_H_
#define _WIFI_RECONNECT_H_
#include "FreeRTOS.h"
#include "task.h"
#include <platform_autoconf.h>
#include "os_wrapper.h"
#include "platform_stdlib.h"
#include "basic_types.h"
#include "wifi_api.h"
#include "lwip_netconf.h"
#include "sdk_compat.h"

#define RETRY_LIMIT            10
#define RETRY_INTERVAL        5000    // ms
#define SSID                "YOUR_WIFI_SSID"	// Replace with your Wi-Fi SSID
#define PASSWORD            "YOUR_WIFI_PASSWORD"

#define LED1_PIN _PA_14 //G
#define LED2_PIN _PA_15 //R

void gpio_led_init(void);
void gpio_toggle(u32 GPIO_Pin, int time_ms);
int  user_WiFi_connect(void);
void WiFi_connect_task(void);
void WiFi_reconnect_task(void);
void WiFi_join_status_event_hdl(u8* evt_info);

#endif
