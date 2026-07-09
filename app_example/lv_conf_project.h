/**
 * @file lv_conf_project.h
 * Project-specific LVGL configuration override.
 *
 * This file is used via LV_CONF_PATH to replace the SDK's default lv_conf.h.
 * It includes the SDK original and overrides font settings to make more
 * fonts available without modifying SDK files.
 *
 * Include chain:
 *   lvgl/src/lv_conf_internal.h
 *     → #include LV_CONF_PATH  (= this file)
 *       → #include "lv_conf.h" (SDK's port/amebadplus/lv_conf.h)
 *       → #undef/#define font overrides
 *     → lv_conf_internal.h defaults (skipped because already defined)
 */

 /* Include the SDK's original lv_conf.h from the global include path.
  * port/amebadplus/ is globally included by the LVGL component's lvgl.cmake. */
#include "lv_conf.h"

#undef LV_USE_PERF_MONITOR
#define LV_USE_PERF_MONITOR 0

  /* ========================================================================
   * Color — override to 32-bit ARGB8888 to match LCDC HW format.
   * LCDC is configured as LDC_IMG_FMT_ARGB8888, and the frame buffer
   * is allocated as w*h*4 bytes.  SDK default is 16-bit RGB565.
   * ======================================================================== */
#undef  LV_COLOR_DEPTH
#define LV_COLOR_DEPTH 32

   /* 32-bit color doesn't need byte swap; undef to avoid confusion. */
#undef  LV_COLOR_16_SWAP

/* ========================================================================
 * Memory — quad the pool: canvas draw_buf (78KB) + ~100 widgets + layout transitions
 * SDK default: 64KB, was 128KB before fragmentation analysis
 * ======================================================================== */
#undef  LV_MEM_SIZE
#define LV_MEM_SIZE (256 * 1024U)

 /* ========================================================================
  * Font Overrides — enable full Montserrat font family
  * SDK default only enables 14/20/24/26.
  * ======================================================================== */

#undef  LV_FONT_MONTSERRAT_8
#define LV_FONT_MONTSERRAT_8  1

#undef  LV_FONT_MONTSERRAT_10
#define LV_FONT_MONTSERRAT_10 1

#undef  LV_FONT_MONTSERRAT_12
#define LV_FONT_MONTSERRAT_12 1

  /* 14 is already 1 in SDK, keep as-is */

#undef  LV_FONT_MONTSERRAT_16
#define LV_FONT_MONTSERRAT_16 1

#undef  LV_FONT_MONTSERRAT_18
#define LV_FONT_MONTSERRAT_18 1

/* 20 is already 1 in SDK, keep as-is */

#undef  LV_FONT_MONTSERRAT_22
#define LV_FONT_MONTSERRAT_22 1

/* 24 is already 1 in SDK, keep as-is */

/* 26 is already 1 in SDK, keep as-is */

#undef  LV_FONT_MONTSERRAT_28
#define LV_FONT_MONTSERRAT_28 1

#undef  LV_FONT_MONTSERRAT_30
#define LV_FONT_MONTSERRAT_30 1

#undef  LV_FONT_MONTSERRAT_32
#define LV_FONT_MONTSERRAT_32 1

#undef  LV_FONT_MONTSERRAT_34
#define LV_FONT_MONTSERRAT_34 1

#undef  LV_FONT_MONTSERRAT_36
#define LV_FONT_MONTSERRAT_36 1

#undef  LV_FONT_MONTSERRAT_38
#define LV_FONT_MONTSERRAT_38 1

#undef  LV_FONT_MONTSERRAT_40
#define LV_FONT_MONTSERRAT_40 1

#undef  LV_FONT_MONTSERRAT_42
#define LV_FONT_MONTSERRAT_42 1

#undef  LV_FONT_MONTSERRAT_44
#define LV_FONT_MONTSERRAT_44 1

#undef  LV_FONT_MONTSERRAT_46
#define LV_FONT_MONTSERRAT_46 1

#undef  LV_FONT_MONTSERRAT_48
#define LV_FONT_MONTSERRAT_48 1
