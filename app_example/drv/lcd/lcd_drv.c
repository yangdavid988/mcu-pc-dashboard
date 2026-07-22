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
static void flip_done_cb(void* data);

void lcd_init(void)
{
    lcdc_core_init(SCREEN_CFG);
    lcdc_core_register_flip_done(flip_done_cb);
}

void lcd_get_info(int* width, int* height)
{
    lcdc_core_get_info(width, height);
}

void lcd_flush_buffer(uint8_t* buffer)
{
    lcdc_core_flush_buffer(buffer);
}

void lcd_get_fb_base(uint32_t* base1, uint32_t* base2)
{
    int w, h;
    lcdc_core_get_info(&w, &h);
    uint32_t buf_size = (uint32_t)(w * h * 4);  /* ARGB8888: 4 bytes/pixel */
    uint32_t base = lcdc_core_get_fb_base();     /* section-allocated PSRAM, not cfg->fb_base */

    if (base1 != NULL)
        *base1 = base;
    if (base2 != NULL)
        *base2 = base + buf_size;
}

const char* lcd_get_driver_name(void)
{
    return SCREEN_CFG->name;
}

/* ========================================================================
 * LVGL integration callbacks
 * ======================================================================== */

 /** VBlank flip-done callback: notify LVGL that frame buffer has switched (DMA address updated) */
static void flip_done_cb(void* data)
{
    lv_display_t* disp = (lv_display_t*)data;
    lv_display_flush_ready(disp);
}

void lvgl_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p)
{
    LV_UNUSED(area);
 /*
     * Two-stage flush (record + commit):
     *
     * LVGL 9.3 DIRECT + 2-buffer mode: flush_cb is called after each
     * invalidated region is rendered.  DCache_Clean is done here but
     * the pending flip is NOT set — deferred to lcdc_core_flush_commit()
     * which must be called after lv_timer_handler() or lv_refr_now()
     * completes, when ALL dirty rects in this batch are done.
     *
     * Flow:
     *   1. lcdc_core_record_flush() → DCache_Clean + record buffer
     *   2. lv_display_flush_ready() → unblock LVGL (avoids deadlock:
     *      LVGL sets disp->flushing=1 before flush_cb and waits for
     *      flush_ready; without it the timer handler never returns)
     *   3. [later] lcdc_core_flush_commit() → set pending flip for LINE ISR
     *   4. LINE @ row 400 → DMA switch via ShadowReload (effective VBlank)
     *   5. FRD ISR → flip_done_cb (no lv_display_flush_ready — already done)
     *
     * Separating "record" from "commit" prevents write-collision when
     * LVGL processes multiple non-contiguous dirty rects within one
     * lv_refr_now batch:
     *   - Within one batch, ALL rects render to the SAME buffer
     *   - flush_cb per rect → DCache_Clean + record (same fb_addr each call)
     *   - flush_ready tells LVGL to continue to the next rect (same buffer)
     *   - After batch complete, flush_commit sets the DMA pending flip
     *   - DMA switch only happens on the next LINE @ row 400 → no mid-frame
     *     collision between LVGL render and DMA scan-out                     */
    lcdc_core_record_flush((uint32_t)color_p, disp);
    lv_display_flush_ready(disp);   /* unblock LVGL flushing state */
    lcdc_core_count_flush();
}

uint32_t custom_tick_get(void)
{
    /* rtos_time_get_current_system_time_ms() is ISR-safe — uses
     * xTaskGetTickCountFromISR() in interrupt context, xTaskGetTickCount() otherwise. */
    return rtos_time_get_current_system_time_ms();
}
