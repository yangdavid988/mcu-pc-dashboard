#include "ui/pc_dashboard_theme.h"
#include "ui/pc_dashboard_layout.h"
#include "config/threshold_config.h"
#include "assets/backgrounds/bg.h"
#include "log.h"

#ifndef TAG
#define TAG "V3_THEME"
#endif

/* ========================================================================
 * Global state
 * ======================================================================== */
layout_id_t g_layout_id = DEFAULT_LAYOUT;
theme_id_t  g_theme_id  = DEFAULT_THEME;

/* ========================================================================
 * Theme color tables
 * ======================================================================== */

/* --- COBALT: blue geometric palette --- */

const theme_t g_themes[THEME_MAX] = {
    /* THEME_COBALT */
    {
        .name     = THEME_NAME_COBALT,
        .cpu      = LV_COLOR_MAKE(0x00, 0x88, 0xFF),
        .ram      = LV_COLOR_MAKE(0xFF, 0x88, 0x00),
        .disk     = LV_COLOR_MAKE(0xFF, 0x55, 0x55),
        .batt     = LV_COLOR_MAKE(0x44, 0xDD, 0x44),
        .gpu      = LV_COLOR_MAKE(0xBB, 0x44, 0xEE),
        .io       = LV_COLOR_MAKE(0x22, 0xBB, 0xCC),
        .net      = LV_COLOR_MAKE(0x00, 0xCC, 0x88),
        .sys      = LV_COLOR_MAKE(0x88, 0x88, 0xFF),
        .header   = LV_COLOR_MAKE(0x00, 0xCC, 0xFF),
        .env      = LV_COLOR_MAKE(0x44, 0xEE, 0x88),
        .bg_top   = LV_COLOR_MAKE(0x0E, 0x0E, 0x28),
        .bg_bot   = LV_COLOR_MAKE(0x06, 0x06, 0x16),
        .warn     = LV_COLOR_MAKE(0xFF, 0x44, 0x44),
        .bg_image = &bg_cobalt,
    },

    /* THEME_INFERNO */
    {
        .name     = THEME_NAME_INFERNO,
        .cpu      = LV_COLOR_MAKE(0xFF, 0x33, 0x33),
        .ram      = LV_COLOR_MAKE(0xFF, 0x66, 0x33),
        .disk     = LV_COLOR_MAKE(0xCC, 0x44, 0x44),
        .batt     = LV_COLOR_MAKE(0xFF, 0x99, 0x33),
        .gpu      = LV_COLOR_MAKE(0xDD, 0x33, 0x55),
        .io       = LV_COLOR_MAKE(0xDD, 0x66, 0x44),
        .net      = LV_COLOR_MAKE(0xEE, 0x77, 0x33),
        .sys      = LV_COLOR_MAKE(0xCC, 0x55, 0x55),
        .header   = LV_COLOR_MAKE(0xFF, 0x44, 0x44),
        .env      = LV_COLOR_MAKE(0xFF, 0x88, 0x33),
        .bg_top   = LV_COLOR_MAKE(0x1A, 0x0A, 0x0A),
        .bg_bot   = LV_COLOR_MAKE(0x0E, 0x05, 0x05),
        .warn     = LV_COLOR_MAKE(0xFF, 0x66, 0x66),
        .bg_image = &bg_inferno,
    },

    /* THEME_SILICON */
    {
        .name     = THEME_NAME_SILICON,
        .cpu      = LV_COLOR_MAKE(0x8A, 0x8A, 0xB0),
        .ram      = LV_COLOR_MAKE(0xA0, 0xA0, 0xBB),
        .disk     = LV_COLOR_MAKE(0x88, 0x88, 0xAA),
        .batt     = LV_COLOR_MAKE(0x88, 0xBB, 0x88),
        .gpu      = LV_COLOR_MAKE(0x99, 0x66, 0xCC),
        .io       = LV_COLOR_MAKE(0x66, 0xAA, 0xBB),
        .net      = LV_COLOR_MAKE(0x66, 0xBB, 0x99),
        .sys      = LV_COLOR_MAKE(0x99, 0x99, 0xCC),
        .header   = LV_COLOR_MAKE(0xA0, 0xA0, 0xC0),
        .env      = LV_COLOR_MAKE(0x88, 0xCC, 0xAA),
        .bg_top   = LV_COLOR_MAKE(0x1A, 0x1A, 0x24),
        .bg_bot   = LV_COLOR_MAKE(0x0E, 0x0E, 0x16),
        .warn     = LV_COLOR_MAKE(0xCC, 0x44, 0x44),
        .bg_image = &bg_silicon,
    },
};

/* ========================================================================
 * Theme / layout name lookup
 * ======================================================================== */
const char* theme_get_name(theme_id_t id)
{
    if (id >= THEME_MAX)
        return "UNKNOWN";
    return g_themes[id].name;
}

const char* layout_get_name(layout_id_t id)
{
    switch (id)
    {
        case LAYOUT_TRIAD:
            return LAYOUT_NAME_TRIAD;
        case LAYOUT_VORTEX:
            return LAYOUT_NAME_VORTEX;
        case LAYOUT_PULSE:
            return LAYOUT_NAME_PULSE;
        default:
            return "UNKNOWN";
    }
}

/* ========================================================================
 * Theme / layout switch
 * ======================================================================== */

/* Watermark image (persistent, on TOP of layout as subtle overlay) */
static lv_obj_t* g_bg_watermark = NULL;

static void theme_watermark_update(void)
{
    const theme_t* t = &g_themes[g_theme_id];
    if (g_bg_watermark == NULL)
    {
        g_bg_watermark = lv_obj_create(lv_scr_act());
        lv_obj_set_style_bg_opa(g_bg_watermark, LV_OPA_0, 0);
        lv_obj_set_style_border_width(g_bg_watermark, 0, 0);
        lv_obj_set_style_radius(g_bg_watermark, 0, 0);
    }

    lv_obj_set_style_bg_image_src(g_bg_watermark, t->bg_image, 0);
    lv_obj_set_style_bg_image_opa(g_bg_watermark, LV_OPA_10, 0);

    if (g_theme_id == THEME_SILICON)
    {
        /* Centered, non-tiled watermark for SILICON theme */
        lv_obj_set_size(g_bg_watermark, 200, 200);
        lv_obj_set_pos(g_bg_watermark,
                       (SCREEN_WIDTH - 200) / 2,
                       (SCREEN_HEIGHT - 200) / 2);
        lv_obj_set_style_bg_image_tiled(g_bg_watermark, false, 0);
    }
    else
    {
        /* Full-screen tiled watermark for blue / red themes */
        lv_obj_set_size(g_bg_watermark, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_pos(g_bg_watermark, 0, 0);
        lv_obj_set_style_bg_image_tiled(g_bg_watermark, true, 0);
    }

    /* Move to front so it's visible above the layout widgets */
    lv_obj_move_foreground(g_bg_watermark);
}

static void theme_apply_background(void)
{
    const theme_t* t = &g_themes[g_theme_id];

    /* Screen gradient */
    lv_obj_set_style_bg_color(lv_scr_act(), t->bg_top, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), t->bg_bot, 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);

    if (g_theme_id == THEME_SILICON)
    {
        /* SILICON: gradient only — centered image goes in watermark layer */
        lv_obj_set_style_bg_image_src(lv_scr_act(), NULL, 0);
        return;
    }

    /* Full-screen tiled bg image (behind all widgets, very subtle) */
    lv_obj_set_style_bg_image_src(lv_scr_act(), t->bg_image, 0);
    lv_obj_set_style_bg_image_opa(lv_scr_act(), LV_OPA_10, 0);
    lv_obj_set_style_bg_image_tiled(lv_scr_act(), true, 0);
}

/* ========================================================================
 * Layout / theme switch animation (fade-out old -> fade-in new)
 * ======================================================================== */
static layout_id_t s_pending_layout = LAYOUT_MAX;
static theme_id_t  s_pending_theme  = THEME_MAX;
static bool        g_switching      = false; /* Anti-reentry guard */

/** Simple switch: destroy old layout, create new one.
 *  Background is updated FIRST so the brief flash between
 *  destroy and create shows the correct theme gradient, not a black screen.
 *  Canvas (Vortex CPU ring) is handled independently by its own
 *  particle timer — switch doesn't touch canvas rendering at all. */
static void start_switch(void)
{
    /* 1. Update theme ID (background color/image) BEFORE destroy,
     *    so the bare screen behind the soon-to-be-destroyed layout
     *    already shows the correct gradient — no black flash. */
    if (s_pending_theme < THEME_MAX)
    {
        g_theme_id       = s_pending_theme;
        s_pending_theme  = THEME_MAX;
    }
    theme_apply_background();

    /* 2. Destroy old layout — g_layout_id is STILL the old value,
     *    so destroy_current_layout() correctly finds and frees
     *    g_layout_containers[old_id] including its particle timer. */
    destroy_current_layout();

    /* 3. NOW adopt the new layout ID (must happen AFTER destroy) */
    if (s_pending_layout < LAYOUT_MAX)
    {
        g_layout_id      = s_pending_layout;
        s_pending_layout = LAYOUT_MAX;
    }

    /* 4. Create new layout */
    switch (g_layout_id)
    {
        case LAYOUT_TRIAD:
            create_layout_triad(lv_scr_act());
            break;
        case LAYOUT_VORTEX:
            create_layout_vortex(lv_scr_act());
            break;
        case LAYOUT_PULSE:
            create_layout_pulse(lv_scr_act());
            break;
        default:
            break;
    }
    theme_watermark_update();
    notify_layout_switched();
    /* Raise sedentary edge strips above newly-created layout widgets.
     * This is safe even if strips haven't been created yet (create
     * time is during first MONITOR entry, not at boot).            */
    sedentary_flash_raise();
    /* Recolor the strips to the newly-active theme's warn colour so the
     * glow follows the theme instead of keeping the create-time colour. */
    sedentary_flash_recolor();
    g_switching = false;
}

void theme_watermark_show(bool show)
{
    if (g_bg_watermark == NULL)
        return;
    /* Use HIDDEN flag so the watermark is skipped in LVGL rendering
     * (bg_opa=0 alone does NOT suppress bg_image rendering in LVGL 9.3). */
    if (show)
        lv_obj_remove_flag(g_bg_watermark, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(g_bg_watermark, LV_OBJ_FLAG_HIDDEN);
}

void layout_switch(layout_id_t layout)
{
    if (g_switching)
        return; /* Animation in progress, ignore */
    g_switching      = true;
    s_pending_layout = layout;
    s_pending_theme  = THEME_MAX;
    RTK_LOGI(TAG, "layout_switch -> %s\n", layout_get_name(layout));
    start_switch();
}

void theme_switch(theme_id_t theme)
{
    if (g_switching)
        return;
    g_switching      = true;
    s_pending_theme  = theme;
    s_pending_layout = LAYOUT_MAX;
    RTK_LOGI(TAG, "theme_switch -> %s\n", theme_get_name(theme));
    start_switch();
}
