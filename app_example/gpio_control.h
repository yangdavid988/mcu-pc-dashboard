#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

void gpio_control_init(void);

/* Process deferred GPIO switch requests (call from LVGL timer context, not ISR) */
void gpio_control_process(void);

/* Brightness OSD feedback — called from gpio_control_process when brightness changes */
void brightness_osd_show(int percent);

#endif
