#ifndef PC_DASHBOARD_THEME_H
#define PC_DASHBOARD_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Layout enum
 * ======================================================================== */
typedef enum
{
    LAYOUT_TRIAD = 0,   /* 3-column matrix */
    LAYOUT_VORTEX = 1,  /* CPU centered */
    LAYOUT_PULSE = 2,   /* 2x3 HUD grid */
    LAYOUT_MAX
} layout_id_t;

/* ========================================================================
 * Theme enum
 * ======================================================================== */
typedef enum
{
    THEME_COBALT = 0,   /* Intel Blue */
    THEME_INFERNO = 1,  /* AMD Red */
    THEME_SILICON = 2,  /* Apple Silver */
    THEME_MAX
} theme_id_t;

/* ========================================================================
 * Defaults
 * ======================================================================== */
#ifndef DEFAULT_LAYOUT
#define DEFAULT_LAYOUT  LAYOUT_TRIAD
#endif
#ifndef DEFAULT_THEME
#define DEFAULT_THEME   THEME_COBALT
#endif

/* ========================================================================
 * Brand names
 * ======================================================================== */
#define LAYOUT_NAME_TRIAD   "TRIAD"
#define LAYOUT_NAME_VORTEX  "VORTEX"
#define LAYOUT_NAME_PULSE   "PULSE"
#define THEME_NAME_COBALT   "COBALT"
#define THEME_NAME_INFERNO  "INFERNO"
#define THEME_NAME_SILICON  "SILICON"

/* ========================================================================
 * Theme color table
 * ======================================================================== */
typedef struct
{
    const char *name;
    lv_color_t cpu;
    lv_color_t ram;
    lv_color_t disk;
    lv_color_t batt;
    lv_color_t gpu;
    lv_color_t io;
    lv_color_t net;
    lv_color_t sys;
    lv_color_t header;
    lv_color_t env;
    lv_color_t bg_top;
    lv_color_t bg_bot;
    lv_color_t warn;
    const void *bg_image;  /* LVGL image descriptor, NULL = gradient only */
} theme_t;

/* ========================================================================
 * Globals
 * ======================================================================== */
extern layout_id_t g_layout_id;
extern theme_id_t  g_theme_id;
extern const theme_t g_themes[THEME_MAX];

/* ========================================================================
 * Functions
 * ======================================================================== */
void theme_switch(theme_id_t theme);
void layout_switch(layout_id_t layout);
const char *theme_get_name(theme_id_t id);
const char *layout_get_name(layout_id_t id);
void theme_watermark_show(bool show);

#ifdef __cplusplus
}
#endif

#endif /* PC_DASHBOARD_THEME_H */
