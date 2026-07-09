#ifndef LCDC_CORE_H
#define LCDC_CORE_H

#include <stdint.h>

/* ========================================================================
 * Image format enumeration
 * ======================================================================== */
typedef enum {
    LDC_IMG_FMT_RGB565,
    LDC_IMG_FMT_RGB888,
    LDC_IMG_FMT_ARGB8888,
} lcdc_image_format_t;

/* ========================================================================
 * Screen configuration table — each screen provides pinmux + 6 timing params
 * ======================================================================== */
typedef struct {
    /* LCD timing parameters (HV mode) */
    uint8_t  vsw;
    uint8_t  vbp;
    uint8_t  vfp;
    uint8_t  hsw;
    uint8_t  hbp;
    uint8_t  hfp;

    /* Image format */
    lcdc_image_format_t image_format;

    /* Pin configuration function (provided by cfg, differs per screen) */
    void     (*pinmux_config)(void);

    /* Backlight/reset init (optional, DBL070 needs extra delay) */
    void     (*backlight_init)(void);

    /* Driver name (for logging) */
    const char *name;

    /* Frame buffer base address (PSRAM address) */
    uint32_t fb_base;
} lcdc_screen_cfg_t;

/* ========================================================================
 * Generic LCDC core API
 * ======================================================================== */

/* Initialize with screen configuration table */
void lcdc_core_init(const lcdc_screen_cfg_t *cfg);

/* Flush buffer (DCache_Clean + DMA trigger) */
void lcdc_core_flush_buffer(uint8_t *buffer);

/* Get resolution */
void lcdc_core_get_info(int *width, int *height);

/* Register VBlank callback */
void lcdc_core_register_vblank(void (*cb)(void *), void *data);

/* Set DMA buffer pointer + trigger refresh only (no DCache_Clean, caller's responsibility) */
void lcdc_core_trigger_refresh(uint8_t *buffer);

/* VBlank sync: mark dirty region, defer DCache flush to VBlank ISR (eliminates tearing) */
void lcdc_core_mark_dirty(uint32_t off, uint32_t len);

/* Force immediate DCache flush + DMA update (for initial frame) */
void lcdc_core_flush_now(uint32_t fb_addr);

/* ========================================================================
 * Double-buffer page flip API
 * ======================================================================== */

/* Set pending flip frame buffer address (DMA switch executed in VBlank ISR) */
void lcdc_core_set_pending_flip(uint32_t fb_addr, void *context);

/* Register flip-done callback (called after DMA switch in VBlank ISR) */
void lcdc_core_register_flip_done(void (*cb)(void *));

/* ========================================================================
 * Debug API (for diagnostic tearing issues)
 * ======================================================================== */

/* Record one flush_cb call (called by lcd_drv.c, stats flush->flip latency) */
void lcdc_core_debug_flush_called(void);

/* ========================================================================
 * Diagnostic getters (always-on, for cross-task freeze detection)
 * ======================================================================== */

/* Returns last FRD (frame-done) ISR tick in ms */
uint32_t lcdc_core_get_last_frd_tick(void);

/* Returns last LINE ISR tick in ms */
uint32_t lcdc_core_get_last_line_tick(void);

/* Returns last flip (pending consumed by LINE) tick in ms */
uint32_t lcdc_core_get_last_flip_tick(void);

/* Returns FRD interrupt count (number of frames completed) */
uint32_t lcdc_core_get_frd_count(void);

/* Returns number of page flips (LINE consumed pending_flip) */
uint32_t lcdc_core_get_flip_count(void);

/* Returns pend_overwrite count */
uint32_t lcdc_core_get_pend_overwrite(void);

#endif /* LCDC_CORE_H */
