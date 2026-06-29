#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

void gpio_control_init(void);

/* Process deferred GPIO switch requests (call from LVGL timer context, not ISR) */
void gpio_control_process(void);

#endif
