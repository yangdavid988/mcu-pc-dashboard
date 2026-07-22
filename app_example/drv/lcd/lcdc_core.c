/*
 * Copyright (c) 2025 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "lcdc_core.h"
#include "ameba_soc.h"
#include "os_wrapper.h"
#include "string.h"
#include "stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

 /* ========================================================================
  * Debug switch
  * ======================================================================== */
#define LCDC_DEBUG_ENABLE       0       /* 0=disable (was 1 during root-cause investigation; ISR logging causes Heisenbug + ISR-safety concern) */

  /* ========================================================================
   * Constants
   * ======================================================================== */
#define WIDTH                       800
#define HEIGHT                      480
#define LCDC_LINE_NUM_INTR_DEF      (HEIGHT * 5 / 6)   /* line 400 */
#define FB_BUF_SIZE                 (WIDTH * HEIGHT * 4)/* 1,536,000 bytes per buffer */

  /* ========================================================================
   * PSRAM static framebuffer allocation (section attribute)
   *
   * The LVGL double-buffer is placed in the .psram_heap.start section
   * (KM4TZ_BD_PSRAM, NOLOAD) which is the only NOLOAD section that maps
   * to the linker-managed PSRAM region.  This achieves two goals without
   * modifying the SDK linker script:
   *
   * 1. The linker reserves the space — __psram_heap_buffer_start__
   *    advances past the arrays, so even if a future runtime enables a
   *    PSRAM heap (currently __psram_heap_buffer_size__ = 0), the heap
   *    cannot overlap with framebuffer memory.
   *
   * 2. NOLOAD prevents flash-image bloat (BSS-like zero-fill at boot).
   *
   * The actual framebuffer base address is read via lcdc_core_get_fb_base()
   * at runtime, replacing the old hardcoded cfg->fb_base.  LCDC DMA,
   * LVGL registration, and buffer flush all use this dynamic address.
   * ======================================================================== */
__attribute__((section(".psram_heap.start")))
__attribute__((aligned(64)))
static volatile uint8_t s_lvgl_fb_pool[FB_BUF_SIZE * 2];  /* 2 × 1.5 MB = 3 MB */

uint32_t lcdc_core_get_fb_base(void)
{
    return (uint32_t)s_lvgl_fb_pool;
}

   /* ========================================================================
    * Internal state
    * ======================================================================== */
static const lcdc_screen_cfg_t* g_cfg = NULL;
static volatile u8* g_buffer = NULL;
static volatile int g_refresh = 0;

/* VBlank sync: accumulate dirty region, flush DCache during VBlank (eliminate tearing) */
static volatile u32 g_dirty_start = 0;
static volatile u32 g_dirty_end = 0;
static volatile int g_dirty_pending = 0;

static struct {
    u32 IrqNum;
    u32 IrqData;
    u32 IrqPriority;
} gLcdcIrqInfo;

/* VBlank callback (direct function pointer, no struct wrapper needed) */
static void (* volatile g_vblank_cb)(void*) = NULL;
static void* volatile g_data = NULL;

/* Stage 1: recorded by flush_cb (DCache_Clean done, no pendig flip) */
static volatile uint32_t g_pending_flush_fb = 0;
static void* volatile g_pending_flush_ctx = NULL;

/* Stage 2: committed pending flip — consumed by LINE ISR @ row 400 */
static volatile uint32_t g_pending_flip_fb = 0;
static void* volatile g_pending_context = NULL;

/* flip_done callback (registered by lcd_drv.c) */
static void (* volatile g_flip_done_cb)(void*) = NULL;

/* Deferred flip_done_cb: LINE ISR sets this, FRD ISR consumes it */
static void* volatile g_flip_done_deferred_ctx = NULL;

/* ========================================================================
 * Debug counters
 * ======================================================================== */

typedef struct {
    /* Event counts */
    uint32_t frd_count;
    uint32_t flip_count;
    uint32_t flush_count;
    uint32_t underflow_count;

    /* Edge cases */
    uint32_t pend_overwrite;    /* New pending overwrote unprocessed pending */

    /* Timestamps (ms) */
    uint32_t last_flush_tick;
    uint32_t last_frd_tick;
    uint32_t last_flip_tick;

    /* LINE interrupt tracking (fires even without pending flip) */
    uint32_t line_count;
    uint32_t last_line_tick;

    /* flush->flip latency stats */
    uint32_t lat_total;
    uint32_t lat_min;
    uint32_t lat_max;
    uint32_t lat_count;

    uint32_t log_countdown;
} lcdc_debug_t;

static volatile lcdc_debug_t g_dbg = {
    .lat_min = 0xFFFFFFFFU,
    .log_countdown = 60,
};

__attribute__((unused))static void lcdc_debug_log(void)
{
#if LCDC_DEBUG_ENABLE
    uint32_t avg_lat = 0;
    if (g_dbg.lat_count)
    {
        avg_lat = g_dbg.lat_total / g_dbg.lat_count;
    }
    RTK_LOGS(NOTAG, RTK_LOG_ALWAYS,
        "\r\n[LCDC_DBG] "
        "FRD=%d LINE_FLIP=%d FLUSH=%d UNDR=%d | "
        "OVR=%d | "
        "LAT(ms):avg=%d min=%d max=%d | "
        "TICK:FLUSH=%d FRD=%d FLIP=%d\r\n",
        (int)g_dbg.frd_count,
        (int)g_dbg.flip_count, (int)g_dbg.flush_count,
        (int)g_dbg.underflow_count,
        (int)g_dbg.pend_overwrite,
        (int)avg_lat, (int)g_dbg.lat_min, (int)g_dbg.lat_max,
        (int)g_dbg.last_flush_tick, (int)g_dbg.last_frd_tick,
        (int)g_dbg.last_flip_tick);
#else
    (void)0;
#endif /* LCDC_DEBUG_ENABLE */
}

/* ========================================================================
 * IRQ handler — based on official ST7262 SDK implementation
 *
 * Interrupt | Purpose
 * ----------|-----------------------------------
 * FRD       | Monitoring + old path (single-buffer dirty region accumulation)
 * LINE      | ★ Double-buffer page flip (official recommended timing)
 * DMA_UN    | Underflow warning
 * ======================================================================== */
static void lcdc_irq_handler(void)
{
    volatile u32 IntId;

    IntId = LCDC_GetINTStatus(LCDC);
    LCDC_ClearINT(LCDC, IntId);

    /* -- Frame end interrupt: counters + old path -- */
    if (IntId & LCDC_BIT_LCD_FRD_INTS)
    {
        g_dbg.frd_count++;
        g_dbg.last_frd_tick = rtos_time_get_current_system_time_ms();

        /* [Old path] Single buffer + dirty region accumulation */
        if (g_dirty_pending || g_refresh)
        {
            if (g_dirty_pending)
            {
                u32 len = g_dirty_end - g_dirty_start;
                if (len > 0)
                {
                    DCache_Clean(g_dirty_start, len);
                }
                g_dirty_pending = 0;
            }
            g_refresh = 0;
            LCDC_DMAImgCfg(LCDC, (u32)g_buffer);
            LCDC_ShadowReloadConfig(LCDC);
        }

        /* Deferred flip_done_cb (DMA address set in LINE, effective after VBlank) */
        if (g_flip_done_deferred_ctx)
        {
            void* ctx = (void*)g_flip_done_deferred_ctx;
            g_flip_done_deferred_ctx = NULL;
            if (g_flip_done_cb)
            {
                g_flip_done_cb(ctx);
            }
        }
    }

    /* -- LINE interrupt: double-buffer DMA address pre-set (official SDK recommended method) --
     * LINE triggers at row 400 (~83% of frame), ~80 rows + VBlank remaining,
     * uses ShadowReload(VBR) to let HW switch DMA address at VBlank.
     *
     * Note: flip_done_cb (-> lv_display_flush_ready) is NOT called in LINE,
     * but deferred to FRD (when frame truly ends). If called immediately in LINE, LVGL
     * starts writing to the other buffer, but DMA is still reading it before VBlank -> tearing (~3ms window). */
    if (IntId & LCDC_BIT_LCD_LIN_INTS)
    {
        /* Track every LINE interrupt (even without pending flip — for stall detection) */
        g_dbg.line_count++;
        g_dbg.last_line_tick = rtos_time_get_current_system_time_ms();

        if (g_pending_flip_fb)
        {
            LCDC_DMAImgCfg(LCDC, g_pending_flip_fb);
            LCDC_ShadowReloadConfig(LCDC);

            /* Flip counters + flush→flip latency (rtos_time_* is ISR-safe: uses xTaskGetTickCountFromISR) */
            {
                g_dbg.flip_count++;
                g_dbg.last_flip_tick = rtos_time_get_current_system_time_ms();

                uint32_t lat = g_dbg.last_flip_tick - g_dbg.last_flush_tick;
                g_dbg.lat_total += lat;
                g_dbg.lat_count++;
                if (lat < g_dbg.lat_min) g_dbg.lat_min = lat;
                if (lat > g_dbg.lat_max) g_dbg.lat_max = lat;

                /* Periodic LCDC debug log (every ~60 flips, ~1s at 60fps) */
                if (g_dbg.log_countdown) { g_dbg.log_countdown--; }
                if (g_dbg.log_countdown == 0)
                {
                    lcdc_debug_log();
                    g_dbg.log_countdown = 60;
                }
            }

            /* Defer flip_done_cb to FRD to avoid ~3ms tearing window */
            g_flip_done_deferred_ctx = (void*)g_pending_context;
            g_pending_flip_fb = 0;
            g_pending_context = NULL;
        }
    }

    /* VBlank notification (official SDK calls back at LINE position, not true VBlank) */
    if (IntId & LCDC_BIT_LCD_LIN_INTEN)
    {
        if (g_vblank_cb)
        {
            g_vblank_cb(g_data);
        }
    }

    if (IntId & LCDC_BIT_DMA_UN_INTS)
    {
        RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "intr: dma udf !!! \r\n");
        g_dbg.underflow_count++;
    }
}

/* ========================================================================
 * LCDC driver initialization (read timing params from config table)
 * ======================================================================== */
static void lcdc_driver_init(const lcdc_screen_cfg_t* cfg)
{
    LCDC_RGBInitTypeDef LCDC_RGBInitStruct;

    LCDC_Cmd(LCDC, DISABLE);
    LCDC_RGBStructInit(&LCDC_RGBInitStruct);

    LCDC_RGBInitStruct.Panel_RgbTiming.RgbVsw = cfg->vsw;
    LCDC_RGBInitStruct.Panel_RgbTiming.RgbVbp = cfg->vbp;
    LCDC_RGBInitStruct.Panel_RgbTiming.RgbVfp = cfg->vfp;

    LCDC_RGBInitStruct.Panel_RgbTiming.RgbHsw = cfg->hsw;
    LCDC_RGBInitStruct.Panel_RgbTiming.RgbHbp = cfg->hbp;
    LCDC_RGBInitStruct.Panel_RgbTiming.RgbHfp = cfg->hfp;

    LCDC_RGBInitStruct.Panel_Init.IfWidth = LCDC_RGB_IF_24_BIT;
    LCDC_RGBInitStruct.Panel_Init.ImgWidth = WIDTH;
    LCDC_RGBInitStruct.Panel_Init.ImgHeight = HEIGHT;

    LCDC_RGBInitStruct.Panel_RgbTiming.Flags.RgbEnPolar = LCDC_RGB_EN_PUL_HIGH_LEV_ACTIVE;
    LCDC_RGBInitStruct.Panel_RgbTiming.Flags.RgbDclkActvEdge = LCDC_RGB_DCLK_FALLING_EDGE_FETCH;
    LCDC_RGBInitStruct.Panel_RgbTiming.Flags.RgbHsPolar = LCDC_RGB_HS_PUL_LOW_LEV_SYNC;
    LCDC_RGBInitStruct.Panel_RgbTiming.Flags.RgbVsPolar = LCDC_RGB_VS_PUL_LOW_LEV_SYNC;

    switch (cfg->image_format)
    {
    case LDC_IMG_FMT_RGB565:
        LCDC_RGBInitStruct.Panel_Init.InputFormat = LCDC_INPUT_FORMAT_RGB565;
        break;
    case LDC_IMG_FMT_ARGB8888:
        LCDC_RGBInitStruct.Panel_Init.InputFormat = LCDC_INPUT_FORMAT_ARGB8888;
        break;
    default:
        LCDC_RGBInitStruct.Panel_Init.InputFormat = LCDC_INPUT_FORMAT_RGB888;
        break;
    }
    LCDC_RGBInitStruct.Panel_Init.OutputFormat = LCDC_OUTPUT_FORMAT_RGB888;
    LCDC_RGBInitStruct.Panel_Init.RGBRefreshFreq = 60;

    LCDC_RGBInit(LCDC, &LCDC_RGBInitStruct);

    LCDC_DMABurstSizeConfig(LCDC, 2);

    /* Set initial DMA frame buffer address to buf2 (not buf1),
     * to avoid conflict between LVGL's first render to buf1 and LCDC scan.
     * On first VBlank, pending_flip(buf1) will switch DMA to buf1.
     * NB: use lcdc_core_get_fb_base() — not cfg->fb_base — because the
     * buffers are now allocated via section attribute in .psram_heap.start. */
    {
        uint32_t _fb = lcdc_core_get_fb_base();
        LCDC_DMAImgCfg(LCDC, _fb + FB_BUF_SIZE);
        LCDC_ShadowReloadConfig(LCDC);

        memset((void*)_fb, 0, FB_BUF_SIZE * 2);
        DCache_Clean(_fb, FB_BUF_SIZE * 2);
    }

    LCDC_LineINTPosConfig(LCDC, LCDC_LINE_NUM_INTR_DEF);
    LCDC_INTConfig(LCDC,
        LCDC_BIT_LCD_FRD_INTEN |
        LCDC_BIT_DMA_UN_INTEN |
        LCDC_BIT_LCD_LIN_INTEN,
        ENABLE);

    LCDC_Cmd(LCDC, ENABLE);
} /* lcdc_driver_init */

/* ========================================================================
 * Public API
 * ======================================================================== */

void lcdc_core_init(const lcdc_screen_cfg_t* cfg)
{
    g_cfg = cfg;
    /* Use section-attribute allocated PSRAM base, not cfg->fb_base */
    g_buffer = (u8*)lcdc_core_get_fb_base();

    if (cfg->backlight_init)
    {
        cfg->backlight_init();
    }

    gLcdcIrqInfo.IrqNum = LCDC_IRQ;
    gLcdcIrqInfo.IrqPriority = INT_PRI_HIGH;
    gLcdcIrqInfo.IrqData = (u32)LCDC;

    cfg->pinmux_config();

    LCDC_RccEnable();

    InterruptRegister((IRQ_FUN)lcdc_irq_handler,
        gLcdcIrqInfo.IrqNum,
        NULL,
        gLcdcIrqInfo.IrqPriority);
    InterruptEn(gLcdcIrqInfo.IrqNum, gLcdcIrqInfo.IrqPriority);

    lcdc_driver_init(cfg);

    /* Reconfigure IRQ events (overrides settings in lcdc_driver_init) */
    LCDC_LineINTPosConfig(LCDC, LCDC_LINE_NUM_INTR_DEF);
    LCDC_INTConfig(LCDC,
        LCDC_BIT_LCD_FRD_INTEN |
        LCDC_BIT_DMA_UN_INTEN |
        LCDC_BIT_LCD_LIN_INTEN,
        ENABLE);

    LCDC_Cmd(LCDC, ENABLE);
}

void lcdc_core_flush_buffer(uint8_t* buffer)
{
    if (!g_cfg)
        return;

    switch (g_cfg->image_format)
    {
    case LDC_IMG_FMT_ARGB8888:
        g_buffer = buffer;
        DCache_Clean((u32)g_buffer, WIDTH * HEIGHT * 4);
        break;
    case LDC_IMG_FMT_RGB888:
        g_buffer = buffer;
        DCache_Clean((u32)g_buffer, WIDTH * HEIGHT * 3);
        break;
    default:
        g_buffer = buffer;
        DCache_Clean((u32)g_buffer, WIDTH * HEIGHT * 2);
        break;
    }
    g_refresh = 1;
}

void lcdc_core_get_info(int* width, int* height)
{
    if (width)  *width = WIDTH;
    if (height) *height = HEIGHT;
}

void lcdc_core_register_vblank(void (*cb)(void*), void* user_data)
{
    g_vblank_cb = cb;
    g_data = user_data;
}

void lcdc_core_trigger_refresh(uint8_t* buffer)
{
    g_buffer = buffer;
    g_refresh = 1;
}

/** Mark dirty region, defer DCache flush to VBlank (eliminate tearing)
 *  off/len = byte offset within frame buffer
 *  Accumulate dirty region range, unified DCache_Clean + trigger refresh in VBlank ISR */
void lcdc_core_mark_dirty(u32 off, u32 len)
{
    if (!g_cfg)
        return;

    u32 fb_size = (u32)WIDTH * HEIGHT * 4;
    /* Clip len to ensure dirty range stays within frame buffer boundary, prevent DCache_Clean overflow */
    if (off < fb_size)
    {
        if (off + len > fb_size)
            len = fb_size - off;
        u32 area_start = lcdc_core_get_fb_base() + off;
        u32 area_end = area_start + len;

        if (!g_dirty_pending)
        {
            g_dirty_start = area_start;
            g_dirty_end = area_end;
            g_dirty_pending = 1;
        }
        else
        {
            if (area_start < g_dirty_start)
                g_dirty_start = area_start;
            if (area_end > g_dirty_end)
                g_dirty_end = area_end;
        }
    }
}

/** Force immediate DCache flush and trigger DMA update (non-VBlank path, for initial frame) */
void lcdc_core_flush_now(uint32_t fb_addr)
{
    DCache_Clean(fb_addr, WIDTH * HEIGHT * 4);
    g_refresh = 1;
}

/* ========================================================================
 * Double-buffer page flip API
 * ======================================================================== */

void lcdc_core_record_flush(uint32_t fb_addr, void* context)
{
    DCache_Clean(fb_addr, (uint32_t)WIDTH * (uint32_t)HEIGHT * 4u);
    __DSB();

    /* Record ONLY — no pending flip */
    taskENTER_CRITICAL();
    if (g_pending_flush_fb)
        g_dbg.pend_overwrite++;
    g_pending_flush_fb = fb_addr;
    g_pending_flush_ctx = context;
    taskEXIT_CRITICAL();
}

void lcdc_core_flush_commit(void)
{
    taskENTER_CRITICAL();
    if (g_pending_flush_fb)
    {
        if (g_pending_flip_fb)
            g_dbg.pend_overwrite++;
        g_pending_flip_fb = g_pending_flush_fb;
        g_pending_context = g_pending_flush_ctx;
        g_pending_flush_fb = 0;
        g_pending_flush_ctx = NULL;
    }
    taskEXIT_CRITICAL();
}

void lcdc_core_register_flip_done(void (*cb)(void*))
{
    g_flip_done_cb = cb;
}

/* ========================================================================
 * Debug API
 * ======================================================================== */

 /** Record one flush_cb call (called by lcd_drv.c, stats flush->flip latency) */
void lcdc_core_debug_flush_called(void)
{
    g_dbg.flush_count++;
    g_dbg.last_flush_tick = rtos_time_get_current_system_time_ms();
}

/* ========================================================================
 * Always-on diagnostic getters
 * ======================================================================== */

uint32_t lcdc_core_get_last_frd_tick(void)
{
    return g_dbg.last_frd_tick;
}

uint32_t lcdc_core_get_last_line_tick(void)
{
    return g_dbg.last_line_tick;
}

uint32_t lcdc_core_get_last_flip_tick(void)
{
    return g_dbg.last_flip_tick;
}

uint32_t lcdc_core_get_frd_count(void)
{
    return g_dbg.frd_count;
}

uint32_t lcdc_core_get_flush_count(void)
{
    return g_dbg.flush_count;
}

uint32_t lcdc_core_get_flip_count(void)
{
    return g_dbg.flip_count;
}

uint32_t lcdc_core_get_pend_overwrite(void)
{
    return g_dbg.pend_overwrite;
}

bool lcdc_core_is_flip_pending(void)
{
    return g_pending_flip_fb != 0;
}

