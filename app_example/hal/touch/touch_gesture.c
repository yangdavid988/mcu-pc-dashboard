/*
 * touch_gesture.c — LVGL touch gesture handling for T1720A (touch-only UI)
 *
 * Translates swipe / double-tap gestures into layout switch, theme switch,
 * and brightness control actions (replacing physical GPIO buttons).
 *
 * Gesture mapping:
 *   Swipe LEFT / RIGHT   → cycle layout (forward / backward)
 *   Swipe UP             → brightness UP   (+BL_STEP_PCT)
 *   Swipe DOWN           → brightness DOWN (-BL_STEP_PCT)
 *   Double tap           → cycle theme (forward)
 */

#include "touch_gesture.h"
#include "lvgl.h"
#include "core/pc_dashboard.h"       /* g_screen_state, SCREEN_STATE_MONITOR */
#include "ui/pc_dashboard_theme.h"   /* layout_switch, theme_switch, g_layout_id, g_theme_id */
#include "hal/gpio_control.h"        /* brightness_osd_show */
#include "hal/backlight_ctrl.h"      /* backlight_adjust, backlight_get */
#include "log.h"

#define LOG_TAG "GESTURE"

/* ---- Double-tap detection (LVGL's DOUBLE_CLICKED only goes to obj, not indev) ---- */
#define DOUBLE_TAP_TIMEOUT_MS    400
#define DOUBLE_TAP_DIST_MAX      30

static struct
{
    uint32_t    last_click_tick;
    int16_t     last_x;
    int16_t     last_y;
} s_double_tap;

/* ---------------------------------------------------------------------------
 * Gesture event callback — registered on lv_scr_act()
 * --------------------------------------------------------------------------- */
static void gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        bool in_monitor = (g_screen_state == SCREEN_STATE_MONITOR);

        switch (dir)
        {
        case LV_DIR_LEFT:
            if (in_monitor)
            {
                layout_id_t next = (g_layout_id + 1) % LAYOUT_MAX;
                RTK_LOGI(LOG_TAG, "Swipe LEFT -> layout %d\n", (int) next);
                layout_switch(next);
            }
            break;

        case LV_DIR_RIGHT:
            if (in_monitor)
            {
                layout_id_t prev = (g_layout_id + LAYOUT_MAX - 1) % LAYOUT_MAX;
                RTK_LOGI(LOG_TAG, "Swipe RIGHT -> layout %d\n", (int) prev);
                layout_switch(prev);
            }
            break;

        case LV_DIR_TOP:
            backlight_adjust(BL_STEP_PCT);
            brightness_osd_show(backlight_get());
            RTK_LOGI(LOG_TAG, "Swipe UP -> brightness %d%%\n", backlight_get());
            break;

        case LV_DIR_BOTTOM:
            backlight_adjust(-BL_STEP_PCT);
            brightness_osd_show(backlight_get());
            RTK_LOGI(LOG_TAG, "Swipe DOWN -> brightness %d%%\n", backlight_get());
            break;

        default:
            break;
        }
        return;
    }

    if (code == LV_EVENT_SHORT_CLICKED)
    {
        /* Manual double-tap detection (LVGL's DOUBLE_CLICKED only goes to object) */
        uint32_t now = lv_tick_get();
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);

        int16_t dx = p.x - s_double_tap.last_x;
        int16_t dy = p.y - s_double_tap.last_y;
        uint32_t elapsed = now - s_double_tap.last_click_tick;

        if (elapsed < DOUBLE_TAP_TIMEOUT_MS &&
            dx * dx + dy * dy < DOUBLE_TAP_DIST_MAX * DOUBLE_TAP_DIST_MAX &&
            g_screen_state == SCREEN_STATE_MONITOR)
        {
            RTK_LOGS(LOG_TAG, RTK_LOG_ALWAYS, "Double tap detected!\n");
            theme_id_t next = (g_theme_id + 1) % THEME_MAX;
            RTK_LOGI(LOG_TAG, "Double tap -> theme %d\n", (int) next);
            theme_switch(next);

            s_double_tap.last_click_tick = 0;
            s_double_tap.last_x = 0;
            s_double_tap.last_y = 0;
        }
        else
        {
            s_double_tap.last_click_tick = now;
            s_double_tap.last_x = p.x;
            s_double_tap.last_y = p.y;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Public API — register events on indev (not scr_act) so that click/gesture
 * events reach us even when pressed child widgets don't set EVENT_BUBBLE.
 * --------------------------------------------------------------------------- */
void touch_gesture_init(void)
{
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev)
    {
        RTK_LOGE(LOG_TAG, "No indev available\n");
        return;
    }

    lv_indev_add_event_cb(indev, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    lv_indev_add_event_cb(indev, gesture_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    RTK_LOGI(LOG_TAG, "Touch gesture handler registered on indev\n");
}
