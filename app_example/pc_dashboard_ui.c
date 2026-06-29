#include "pc_dashboard_ui.h"
#include "pc_dashboard_layout.h"
#include "pc_dashboard_theme.h"
#include "gpio_control.h"
#include <math.h>

/*
 * NOTE: V3 layout system (pc_dashboard_layout.c) replaces the V2 static layout
 * functions that were here. The UI is now created by layout_switch() which
 * calls create_layout_triad/vortex/pulse().
 *
 * This file retains only:
 * - Waiting screen (shown before first data arrives)
 * - update_dashboard_ui() — data refresh (will be migrated to V3 in future)
 */

 /* ========================================================================
  * Layout common constants
  * ======================================================================== */
#define LAYOUT_PANEL_HEIGHT       376   /* Main container height (also 3-column panel height) */
#define LAYOUT_CARD_GAP           4     /* Uniform card row/column gap */
#define LAYOUT_CARD_TOP_OFFSET    4     /* First card Y offset = bottom margin, symmetrical */
#define LAYOUT_CARD_LEFT_CH       89    /* Left panel card height */
#define LAYOUT_CARD_MID_GAP       4     /* Middle panel card gap */
#define LAYOUT_RIGHT_NET_CH       100   /* Right NETWORK card height */
#define LAYOUT_CARD_PADDING_H     18    /* Card horizontal padding sum */
#define LAYOUT_CARD_BAR_MARGIN    10    /* Bar inset relative to card edge */
#define LAYOUT_CARD_BAR_H         14    /* Default bar height */

  /* FA icons + Montserrat text use separate labels. create_icon_lbl() creates
   * icon labels independently to avoid LVGL 9.3 fallback issues causing FA icons
   * to display as garbage characters. */

   /* ========================================================================
    * UI component globals
    * ======================================================================== */
lv_timer_t* g_dashboard_timer = NULL;

/* --- Waiting screen --- */
static lv_obj_t* g_waiting_container = NULL;

/* --- Header --- */
static lv_obj_t* g_time_label = NULL;
static lv_obj_t* g_warning_label = NULL; /* Data timeout warning */

/* --- Main container (frames 3 columns) --- */

/* --- Left: resource panels --- */

/* --- Middle: GPU + DISK I/O --- */

/* --- Right: Network + System --- */

/* --- Env panel --- */

/* --- Footer --- */

/* --- FA icon labels (separate from text, uses lv_font_fa_16) --- */
static lv_obj_t* g_icon_warning = NULL;

/* Local time tracking */
uint32_t g_last_displayed_second = 0;
uint32_t g_time_base_ts = 0;
uint32_t g_time_base_ms = 0;

/* MQTT data freshness tracking */
static bool     g_timeout_triggered = false;  /* Avoid duplicate reset triggers */

/* MQTT connection status label (assigned by each layout's create function) */
lv_obj_t* g_mqtt_status_label = NULL;
static int g_mqtt_prev_connected = -1;   /* -1 = uninitialized, ensures first trigger always fires */

void reset_mqtt_status_tracking(void)
{
    g_mqtt_prev_connected = -1;
}

/* ========================================================================
 * Helper functions
 * ======================================================================== */

 /* Create a progress bar with glow effect */

 /* Create a card container (with accent top strip + shadow) */

 /* Update clock display */
static void update_clock_display(void)
{
    if (g_time_label == NULL)
        return;

    if (g_time_base_ts == 0)
    {
        lv_label_set_text(g_time_label, "--:--:--");
        return;
    }

    uint32_t now_ms = rtos_time_get_current_system_time_ms();
    uint32_t elapsed_s = (now_ms - g_time_base_ms) / 1000;
    uint32_t current_ts = g_time_base_ts + elapsed_s + UTC8_OFFSET_SEC;

    if (current_ts == g_last_displayed_second)
        return;
    g_last_displayed_second = current_ts;

    uint16_t y;
    uint8_t mo, d, h, mi, s;
    unix_to_datetime(current_ts, &y, &mo, &d, &h, &mi, &s);

    lv_label_set_text_fmt(g_time_label,
        "%04d-%02d-%02d %02d:%02d:%02d",
        (int)y, (int)mo, (int)d, (int)h, (int)mi, (int)s);
}

/* Update MQTT data timeout warning + connection status + reset defaults on timeout */
static void update_mqtt_warning(void)
{
    /* ---- Timeout detection: reset data ---- */
    if (g_data_last_tick > 0)
    {
        uint32_t now = rtos_time_get_current_system_time_ms();
        uint32_t elapsed = now - g_data_last_tick;

        if (elapsed > 12000)
        {
            /* Timeout > 12s: show warning */
            if (g_warning_label != NULL)
                lv_obj_remove_flag(g_warning_label, LV_OBJ_FLAG_HIDDEN);
            if (g_icon_warning != NULL)
                lv_obj_remove_flag(g_icon_warning, LV_OBJ_FLAG_HIDDEN);

            /* First timeout: reset all data to defaults */
            if (!g_timeout_triggered)
            {
                g_timeout_triggered = true;
                pc_stats_reset_to_default();
            }
        }
        else
        {
            /* Data normal: hide warning, clear timeout flag */
            g_timeout_triggered = false;
            if (g_warning_label != NULL)
                lv_obj_add_flag(g_warning_label, LV_OBJ_FLAG_HIDDEN);
            if (g_icon_warning != NULL)
                lv_obj_add_flag(g_icon_warning, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ---- MQTT connection status label ---- */
    if (g_mqtt_status_label != NULL)
    {
        bool now_connected = g_mqtt_connected;

        if ((int)now_connected != g_mqtt_prev_connected)
        {
            g_mqtt_prev_connected = (int)now_connected;
            if (now_connected)
            {
                lv_label_set_text(g_mqtt_status_label,
                    " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
                lv_obj_set_style_text_color(g_mqtt_status_label,
                    lv_color_make(0x66, 0x88, 0xAA), 0);  /* Original blue-gray */
            }
            else
            {
                lv_label_set_text(g_mqtt_status_label,
                    " System Monitor  |  MQTT Disconnected  |  PC Dashboard v3");
                lv_obj_set_style_text_color(g_mqtt_status_label,
                    lv_color_make(0xFF, 0x33, 0x33), 0);  /* Red warning */
            }
        }
    }
}

/* ========================================================================
 * Create / destroy waiting screen
 * ======================================================================== */
static void create_waiting_ui(void)
{
    g_waiting_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_waiting_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(g_waiting_container, 0, 0);
    lv_obj_set_style_border_width(g_waiting_container, 0, 0);
    lv_obj_set_style_radius(g_waiting_container, 0, 0);
    lv_obj_remove_flag(g_waiting_container, LV_OBJ_FLAG_SCROLLABLE);
    set_gradient_bg(g_waiting_container, lv_color_make(0x08, 0x08, 0x20), lv_color_make(0x02, 0x02, 0x0A));

    /* Waiting text (V3: no FA font icon used) */
    lv_obj_t* wait_icon = lv_label_create(g_waiting_container);
    lv_label_set_text(wait_icon, ">>>\r\n");
    lv_obj_set_style_text_color(wait_icon, lv_color_make(0x00, 0xBB, 0xFF), 0);
    lv_obj_set_style_text_font(wait_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(wait_icon, 350, 170);

    lv_obj_t* wait_label = lv_label_create(g_waiting_container);
    lv_label_set_text(wait_label, "WAITING FOR PC DATA...");
    lv_obj_set_style_text_color(wait_label, lv_color_make(0x00, 0xBB, 0xFF), 0);
    lv_obj_set_style_text_font(wait_label, &lv_font_montserrat_20, 0);
    lv_obj_align(wait_label, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* hint = lv_label_create(g_waiting_container);
    lv_label_set_text(hint, "MQTT: pc/stats | humiture/measurement");
    lv_obj_set_style_text_color(hint, lv_color_make(0x55, 0x55, 0x77), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 50);
}

static void destroy_waiting_ui(void)
{
    if (g_waiting_container != NULL)
    {
        lv_obj_delete(g_waiting_container);
        g_waiting_container = NULL;
    }
}

/* ========================================================================
 * V2 static layout functions removed — replaced by V3 layout_switch()
 * pc_dashboard_layout.c create_layout_triad/vortex/pulse() handles UI creation
 * ======================================================================== */

 /* ========================================================================
  * Build complete UI on data receipt (V3: uses layout_switch to create layout)
  * ======================================================================== */

  /* ========================================================================
   * Create dashboard UI (initial call)
   * ======================================================================== */
void create_dashboard_ui(void)
{
    /* FA icons use create_icon_lbl() for independent labels — no combined font initialization needed */

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_make(0x04, 0x04, 0x10), 0);
    create_waiting_ui();
}

/* ========================================================================
 * Refresh UI data
 * ======================================================================== */

void __attribute__((unused)) dashboard_timer_cb(lv_timer_t* timer)
{
    LV_UNUSED(timer);

    update_clock_display();
    update_layout_clock();
    update_mqtt_warning();

    /* Process deferred GPIO switch requests (ISR-safe: only sets flags) */
    gpio_control_process();

    /* Auto-transition from waiting screen after ~5 seconds even without MQTT data */
    if (!layout_is_created())
    {
        static int wait_ticks = 0;
        if (++wait_ticks >= 5)
        {
            RTK_LOGI("V3_UI", "timeout -> create layout %s (no data)\n",
                layout_get_name(g_layout_id));
            destroy_waiting_ui();
            layout_switch(g_layout_id);
            wait_ticks = 0;
            /* Fall through to update_current_layout with current (reset) data */
        }
        else
        {
            return;  /* Still waiting */
        }
    }

    if (!g_new_data_ready)
    {
        /* No new data, but log every 60th tick (~60s) for alive check */
        static uint32_t alive_tick = 0;
        if (++alive_tick >= 60)
        {
            alive_tick = 0;
            RTK_LOGI("V3_UI", "timer alive, layout=%s theme=%s\n",
                layout_get_name(g_layout_id), theme_get_name(g_theme_id));
        }
        return;
    }

    /* Clear flag first, then refresh UI (avoids missing data arriving during execution) */
    g_new_data_ready = false;

    /* First data: transition from waiting screen to dashboard */
    if (!layout_is_created())
    {
        RTK_LOGI("V3_UI", "first data -> create layout %s\n",
            layout_get_name(g_layout_id));
        destroy_waiting_ui();
        layout_switch(g_layout_id);

        /* Fall through to update_current_layout() with fresh data */
    }

    /* Initialize time base — only on first valid timestamp.
     * Ignore system time (no NTP without network), only use server timestamp
     * and validate sanity (> 1700000000 ≈ 2024).
     * When g_time_base_ts == 0, retry on each data arrival. */
    taskENTER_CRITICAL();
    if (g_time_base_ts == 0)
    {
        if (g_pc_stats.timestamp > 1700000000)
        {
            g_time_base_ts = g_pc_stats.timestamp;
            g_time_base_ms = rtos_time_get_current_system_time_ms();
        }
    }
    taskEXIT_CRITICAL();

    update_current_layout();

    /* Immediately re-apply flash style to prevent gap after update_current_layout() */
    fast_flash_tick();

    toggle_flash_state();
}

/* ========================================================================
 * V2 update_dashboard_ui() removed — replaced by V3 update_current_layout()
 * pc_dashboard_layout.c update_layout_triad/vortex/pulse() handles data refresh
 * ======================================================================== */
