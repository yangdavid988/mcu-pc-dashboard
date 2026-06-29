#ifndef LCD_DRV_H
#define LCD_DRV_H

#include <stdint.h>
#include "lvgl.h"

/* ========================================================================
 * Unified LCD interface
 * ======================================================================== */

 /* Screen initialization (uses ARGB8888 format internally) */
void lcd_init(void);

/* Get screen resolution */
void lcd_get_info(int* width, int* height);

/* Flush buffer (D-Cache Clean + DMA trigger) */
void lcd_flush_buffer(uint8_t* buffer);

/* Get frame buffer base address (returns PSRAM address by screen type) */
void lcd_get_fb_base(uint32_t* base1, uint32_t* base2);

/* Get current screen driver name */
const char* lcd_get_driver_name(void);

/* ========================================================================
 * LVGL integration callbacks
 * ======================================================================== */
void lvgl_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p);
uint32_t custom_tick_get(void);

#endif /* LCD_DRV_H */
