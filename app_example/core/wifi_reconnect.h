#ifndef _WIFI_RECONNECT_H_
#define _WIFI_RECONNECT_H_
#include "FreeRTOS.h"
#include "basic_types.h"
#include "lwip_netconf.h"
#include "os_wrapper.h"
#include "platform_stdlib.h"
#include "config/sdk_compat.h"
#include "task.h"
#include "wifi_api.h"
#include <platform_autoconf.h>

#define RETRY_LIMIT    10
#define RETRY_INTERVAL 5000     // ms
#define SSID           "YOUR_WIFI_SSID" // Replace with your Wi-Fi SSID
#define PASSWORD       "YOUR_WIFI_PASSWORD"

#define LED1_PIN _PA_14 // G
#define LED2_PIN _PA_15 // R

void gpio_led_init(void);
void gpio_toggle(u32 GPIO_Pin, int time_ms);
int  user_wifi_connect(void);
void wifi_connect_task(void);
void wifi_reconnect_task(void);
void wifi_join_status_event_hdl(u8* evt_info);

#endif
