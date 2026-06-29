#include "lcd_drv.h"
#include "lcdc_core.h"
#include "ameba_soc.h"
#include "os_wrapper.h"

/* ========================================================================
 * Screen selection: compile-time switch via #ifdef USE_DBL070
 * Each screen provides one lcdc_screen_cfg_t configuration table
 * ======================================================================== */
#ifdef USE_DBL070
    #include "dbl070_cfg.h"
    #define SCREEN_CFG      (&g_dbl070_cfg)
#else
    #include "st7262_cfg.h"
    #define SCREEN_CFG      (&g_st7262_cfg)
#endif

/* ========================================================================
 * Unified interface implementation
 * ======================================================================== */

/** VBlank flip-done callback: forward-declared for lcd_init registration */
static void flip_done_cb(void *data);

void lcd_init(void)
{
    lcdc_core_init(SCREEN_CFG);
    lcdc_core_register_flip_done(flip_done_cb);
}

void lcd_get_info(int *width, int *height)
{
    lcdc_core_get_info(width, height);
}

void lcd_flush_buffer(uint8_t *buffer)
{
    lcdc_core_flush_buffer(buffer);
}

void lcd_get_fb_base(uint32_t *base1, uint32_t *base2)
{
    int w, h;
    lcdc_core_get_info(&w, &h);
    uint32_t buf_size = (uint32_t)(w * h * 4);  /* ARGB8888: 4 bytes/pixel */

    if (base1 != NULL)
        *base1 = SCREEN_CFG->fb_base;
    if (base2 != NULL)
        *base2 = SCREEN_CFG->fb_base + buf_size;
}

const char *lcd_get_driver_name(void)
{
    return SCREEN_CFG->name;
}

/* ========================================================================
 * LVGL integration callbacks
 * ======================================================================== */

/** VBlank flip-done callback: notify LVGL that frame buffer has switched (DMA address updated) */
static void flip_done_cb(void *data)
{
    lv_display_t *disp = (lv_display_t *)data;
    lv_display_flush_ready(disp);
}

void lvgl_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    LV_UNUSED(area);
    /*
     * Double-buffer DIRECT mode flush:
     *
     * LVGL 9.3 DIRECT + 2-buffer mode: flush_cb is called once per frame (area = full screen, color_p = back buffer address)
     *
     * Flow:
     *   1. DCache_Clean writes CPU-rendered data back to PSRAM (LCDC DMA reads PSRAM, bypasses DCache)
     *   2. lcdc_core_set_pending_flip() registers the pending flip address
     *   3. Does NOT call lv_display_flush_ready() immediately — deferred to VBlank ISR
     *      so LVGL is only notified after DMA has actually switched to the new buffer,
     *      preventing LVGL from writing to the next buffer too early and causing tearing.
     *
     * LVGL blocks in wait_for_flushing() polling during this period.
     * VBlank ISR -> switch DMA -> callback flip_done_cb -> flush_ready -> LVGL continues.
     */
    {
        int w, h;
        lcdc_core_get_info(&w, &h);
        DCache_Clean((uint32_t)color_p, (uint32_t)(w * h * 4));
        __DSB();  /* Ensure cache write buffer fully drained before DMA reads PSRAM */
    }
    lcdc_core_set_pending_flip((uint32_t)color_p, disp);
    lcdc_core_debug_flush_called();
}

uint32_t custom_tick_get(void)
{
    return rtos_time_get_current_system_time_ms();
}
