#ifndef TOUCH_GT911_H
#define TOUCH_GT911_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize GT911 touch controller and register with LVGL.
 * Call once after lcd_init() and lv_display_create().          */
void touch_gt911_init(void);

#endif /* TOUCH_GT911_H */
