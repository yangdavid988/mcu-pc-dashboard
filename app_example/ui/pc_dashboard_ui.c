#include "ui/pc_dashboard_ui.h"
#include "ui/pc_dashboard_layout.h"
#include "ui/pc_dashboard_theme.h"
#include "ui/pc_dashboard_lock_screen.h"
#include "hal/gpio_control.h"
#include "hal/backlight_ctrl.h"
#include "hal/lcd/lcdc_core.h"
#include "core/wifi_reconnect.h" /* g_wifi_retry_* */
// #include <math.h>

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
#define LAYOUT_PANEL_HEIGHT    376 /* Main container height (also 3-column panel height) */
#define LAYOUT_CARD_GAP        4   /* Uniform card row/column gap */
#define LAYOUT_CARD_TOP_OFFSET 4   /* First card Y offset = bottom margin, symmetrical */
#define LAYOUT_CARD_LEFT_CH    89  /* Left panel card height */
#define LAYOUT_CARD_MID_GAP    4   /* Middle panel card gap */
#define LAYOUT_RIGHT_NET_CH    100 /* Right NETWORK card height */
#define LAYOUT_CARD_PADDING_H  18  /* Card horizontal padding sum */
#define LAYOUT_CARD_BAR_MARGIN 10  /* Bar inset relative to card edge */
#define LAYOUT_CARD_BAR_H      14  /* Default bar height */

/* FA icons + Montserrat text use separate labels. create_icon_lbl() creates
 * icon labels independently to avoid LVGL 9.3 fallback issues causing FA icons
 * to display as garbage characters. */

/* ========================================================================
 * UI component globals
 * ======================================================================== */
lv_timer_t* g_dashboard_timer = NULL;

/* --- Waiting screen --- */
static lv_obj_t* g_waiting_container = NULL;

/* --- WiFi retry exhausted popup --- */
static lv_obj_t* g_wifi_popup     = NULL;
static lv_obj_t* g_wifi_popup_msg = NULL; /* label for retry progress text */

/* --- Header --- */
static lv_obj_t* g_time_label    = NULL;
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
uint32_t g_time_base_ts          = 0;
uint32_t g_time_base_ms          = 0;

/* MQTT data freshness tracking */
// static uint32_t g_last_data_tick = 0;   /* System tick (ms) when last data was received */
static bool g_timeout_triggered = false; /* Avoid duplicate reset triggers */

/* MQTT connection status label (assigned by each layout's create function) */
lv_obj_t*  g_mqtt_status_label   = NULL;
static int g_mqtt_prev_connected = -1; /* -1 = uninitialized, ensures first trigger always fires */

#ifdef CONFIG_USB_CDC_MODE
static int s_usb_prev_connected = -1; /* USB CDC state tracking */
#endif

void reset_mqtt_status_tracking(void)
{
    g_mqtt_prev_connected = -1;
#ifdef CONFIG_USB_CDC_MODE
    s_usb_prev_connected = -1;
#endif
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

    uint32_t now_ms     = rtos_time_get_current_system_time_ms();
    uint32_t elapsed_s  = (now_ms - g_time_base_ms) / 1000;
    uint32_t current_ts = g_time_base_ts + elapsed_s + UTC8_OFFSET_SEC;

    if (current_ts == g_last_displayed_second)
        return;
    g_last_displayed_second = current_ts;

    uint16_t y;
    uint8_t  mo, d, h, mi, s;
    unix_to_datetime(current_ts, &y, &mo, &d, &h, &mi, &s);

    lv_label_set_text_fmt(g_time_label,
                          "%04d-%02d-%02d %02d:%02d:%02d",
                          (int) y,
                          (int) mo,
                          (int) d,
                          (int) h,
                          (int) mi,
                          (int) s);
}

/* Update MQTT data timeout warning + connection status + reset defaults on timeout */
static void update_mqtt_warning(void)
{
    /* ---- Timeout detection: reset data ---- */
    if (g_data_last_tick > 0)
    {
        uint32_t now     = rtos_time_get_current_system_time_ms();
        uint32_t elapsed = now - g_data_last_tick;

        if (elapsed > CONNECTION_TIMEOUT_MS)
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
#ifdef CONFIG_USB_CDC_MODE
        /* USB CDC mode: no WiFi/MQTT, show USB status based on data freshness.
         * Uses the same 12s timeout as the NO DATA warning above.            */
        {
            bool usb_connected = (g_data_last_tick > 0) &&
                                 ((rtos_time_get_current_system_time_ms() - g_data_last_tick) <= CONNECTION_TIMEOUT_MS);
            if ((int) usb_connected != s_usb_prev_connected)
            {
                s_usb_prev_connected = (int) usb_connected;
                if (usb_connected)
                {
                    lv_label_set_text(g_mqtt_status_label,
                                      " System Monitor  |  USB Connected  |  PC Dashboard v3");
                    lv_obj_set_style_text_color(g_mqtt_status_label,
                                                lv_color_make(0x66, 0x88, 0xAA),
                                                0); /* Blue-gray */
                }
                else
                {
                    lv_label_set_text(g_mqtt_status_label,
                                      " System Monitor  |  USB Disconnected  |  PC Dashboard v3");
                    lv_obj_set_style_text_color(g_mqtt_status_label,
                                                lv_color_make(0xFF, 0x33, 0x33),
                                                0); /* Red warning */
                }
            }
        }
#else
        /* Check both MQTT socket status AND data freshness:
         * If the MQTT socket is "connected" but no data has arrived for 12s,
         * the PC script either died or is unresponsive — treat as disconnected.
         * This correctly handles the case where pc_to_emqx.py closes but the
         * MCU-to-broker TCP connection stays alive.                         */
        bool now_connected = g_mqtt_connected;
        if (now_connected && g_data_last_tick > 0)
        {
            uint32_t now = rtos_time_get_current_system_time_ms();
            if ((now - g_data_last_tick) > CONNECTION_TIMEOUT_MS)
                now_connected = false;
        }

        if ((int) now_connected != g_mqtt_prev_connected)
        {
            g_mqtt_prev_connected = (int) now_connected;
            if (now_connected)
            {
                lv_label_set_text(g_mqtt_status_label,
                                  " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
                lv_obj_set_style_text_color(g_mqtt_status_label,
                                            lv_color_make(0x66, 0x88, 0xAA),
                                            0); /* Original blue-gray */
            }
            else
            {
                lv_label_set_text(g_mqtt_status_label,
                                  " System Monitor  |  MQTT Disconnected  |  PC Dashboard v3");
                lv_obj_set_style_text_color(g_mqtt_status_label,
                                            lv_color_make(0xFF, 0x33, 0x33),
                                            0); /* Red warning */
            }
        }
#endif /* CONFIG_USB_CDC_MODE */
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
#ifdef CONFIG_USB_CDC_MODE
    lv_label_set_text(hint, "USB CDC: Run PC/pc_to_usb.py");
#else
    lv_label_set_text(hint, "MQTT: pc/stats | humiture/measurement");
#endif
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

void dashboard_timer_cb(lv_timer_t* timer)
{
    LV_UNUSED(timer);

    /* ==================================================================
     * GPIO deferred processing — runs in ALL modes (monitor & clock).
     * Must come before the lock-screen state machine below because the
     * state machine may return early (clock mode) and skip this.
     * ================================================================== */
    gpio_control_process();

    /* Backlight gradual fade tick — runs in ALL modes during fade */
    backlight_fade_tick();

    /* ==================================================================
     * Lock screen state machine — MUST come before layout-dependent calls
     * to avoid accessing dangling widget pointers after destroy.
     * ================================================================== */

    /* Transition: MONITOR → CLOCK */
    if (g_screen_state == SCREEN_STATE_CLOCK && !g_lock_screen_active)
    {
        RTK_LOGI("V3_UI", "lock event -> switching to clock standby\n");
        /* Use theme background only when coming from MONITOR on themes
         * that have a visible bg_image on the screen (A/B — Cobalt, Inferno).
         * Silicon's theme_apply_background() clears bg_image_src to NULL, so
         * it falls back to pure black, same as direct boot. */
        bool use_theme_bg = layout_is_created() &&
                            (lv_obj_get_style_bg_image_src(lv_scr_act(), 0) != NULL);

        if (use_theme_bg)
        {
            /* Preserve theme gradient + bg_image so the transparent
             * container shows the tiled background through the side
             * margins and the 24 px top/bottom strips. */
            const theme_t* t = &g_themes[g_theme_id];
            lv_obj_set_style_bg_color(lv_scr_act(), t->bg_top, 0);
            lv_obj_set_style_bg_grad_color(lv_scr_act(), t->bg_bot, 0);
            lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
        }
        else
        {
            /* Direct boot or Silicon (no bg_image): pure black background,
             * no gradient or background image visible. */
            lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
            lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_NONE, 0);
            lv_obj_set_style_bg_image_src(lv_scr_act(), NULL, 0);
        }
        destroy_current_layout();
        destroy_waiting_ui();
        reset_mqtt_status_tracking();
        /* Destroy WiFi popup if present */
        if (g_wifi_popup != NULL)
        {
            lv_obj_delete(g_wifi_popup);
            g_wifi_popup     = NULL;
            g_wifi_popup_msg = NULL;
        }
        create_lock_screen_clock();
        g_lock_screen_active = true;
        return;
    }

    /* Transition: CLOCK → MONITOR (fade-out transition) */
    if (g_screen_state == SCREEN_STATE_MONITOR && g_lock_screen_active)
    {
        RTK_LOGI("V3_UI", "unlock event -> fade transition to monitor\n");
        /* First create the monitor layout BEHIND the clock UI */
        reset_mqtt_status_tracking();
        g_data_last_tick    = rtos_time_get_current_system_time_ms();
        g_timeout_triggered = false;
        notify_layout_switched();
        destroy_waiting_ui();
        layout_switch(g_layout_id);
        /* Then fade the clock out (async — ready_cb destroys clock widgets) */
        start_unlock_transition();
        return;
    }

    /* Stay in CLOCK mode — just update the clock, skip layout-dependent calls */
    if (g_lock_screen_active)
    {
        update_lock_screen_clock();
        return;
    }

    /* ==================================================================
     * Normal MONITOR mode.
     * Note: layout may be NULL after UNLOCK recovery (destroyed during
     * LOCK transition). Guard all layout-dependent calls below.
     * ================================================================== */
    if (layout_is_created())
    {
        update_clock_display();
        update_layout_clock();
        update_mqtt_warning();
        update_weather_ui();
    }

    /* ---- WiFi retry exhausted popup ---- */
#ifndef CONFIG_USB_CDC_MODE
    if (!g_wifi_connected)
    {
        /* Detect orphan: if popup's parent screen is no longer the active
         * screen (e.g. after waiting → monitor transition via layout_switch),
         * the object was auto-destroyed.  Reset pointers so we re-create. */
        if (g_wifi_popup != NULL && lv_obj_get_screen(g_wifi_popup) != lv_scr_act())
        {
            g_wifi_popup     = NULL;
            g_wifi_popup_msg = NULL;
        }

        if (g_wifi_popup == NULL)
        {
            lv_obj_t* scr = lv_scr_act();

            g_wifi_popup = lv_obj_create(scr);
            lv_obj_set_size(g_wifi_popup, 340, 160);
            lv_obj_center(g_wifi_popup);
            lv_obj_set_style_radius(g_wifi_popup, 12, 0);
            lv_obj_set_style_bg_color(g_wifi_popup, lv_color_make(0x11, 0x11, 0x22), 0);
            lv_obj_set_style_border_color(g_wifi_popup, lv_color_make(0xFF, 0x44, 0x44), 0);
            lv_obj_set_style_border_width(g_wifi_popup, 1, 0);
            lv_obj_set_style_pad_all(g_wifi_popup, 16, 0);
            lv_obj_remove_flag(g_wifi_popup, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_move_foreground(g_wifi_popup);

            /* Icon */
            lv_obj_t* icon = lv_label_create(g_wifi_popup);
            lv_label_set_text(icon, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_color(icon, lv_color_make(0xFF, 0x66, 0x00), 0);
            lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
            lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);

            /* Title */
            lv_obj_t* title = lv_label_create(g_wifi_popup);
            lv_label_set_text(title, "Network Error");
            lv_obj_set_style_text_color(title, lv_color_make(0xFF, 0xFF, 0xFF), 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
            lv_obj_align(title, LV_ALIGN_TOP_LEFT, 40, 2);

            /* Message (updated every tick — progress or exhausted) */
            g_wifi_popup_msg = lv_label_create(g_wifi_popup);
            lv_obj_set_style_text_color(g_wifi_popup_msg, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(g_wifi_popup_msg, &lv_font_montserrat_14, 0);
            lv_obj_align(g_wifi_popup_msg, LV_ALIGN_LEFT_MID, 0, 12);
        }
        else
        {
            /* Keep popup on top — layout_switch may have added new widgets
             * (layout/theme change via GPIO button) which would otherwise
             * cover the popup since they are children of the same screen.   */
            lv_obj_move_foreground(g_wifi_popup);
        }

        /* Update message every tick */
        if (g_wifi_retry_exhausted)
        {
            lv_label_set_text_fmt(g_wifi_popup_msg,
                                  "WiFi connection failed after\n multiple attempts(%d/%d).\n"
                                  "Please check your AP router, and then\n     reboot the device.",
                                  (int) g_wifi_retry_current,
                                  (int) g_wifi_retry_max);
        }
        else
        {
            lv_label_set_text_fmt(g_wifi_popup_msg,
                                  "Reconnecting... (%d/%d)",
                                  (int) g_wifi_retry_current,
                                  (int) g_wifi_retry_max);
        }
    }
    else
    {
        if (g_wifi_popup != NULL)
        {
            lv_obj_delete(g_wifi_popup);
            g_wifi_popup     = NULL;
            g_wifi_popup_msg = NULL;
        }
    }
#endif /* !CONFIG_USB_CDC_MODE */

    /* GPIO deferred processing now runs at top of callback (line 270) */

    /* Auto-transition from waiting screen after ~5 seconds even without MQTT data.
     *
     * Initial-state guard: if pc/event retained msg hasn't arrived yet, delay
     * auto-create to avoid briefly showing MONITOR then flashing to CLOCK
     * when the MCU booted while the PC was locked. Fallback after 15s to
     * handle the case where no PC script is running at all.
     *
     * MQTT mode optimisation: once WiFi connects, skip the wait and switch
     * to monitor immediately — the welcome page is only for WiFi connection. */
    if (!layout_is_created())
    {
        /* MQTT mode: switch to monitor as soon as WiFi is connected */
        if (g_wifi_connected)
        {
            RTK_LOGI("V3_UI", "wifi connected -> create layout %s (no data)\n", layout_get_name(g_layout_id));
            destroy_waiting_ui();
            layout_switch(g_layout_id);
            /* Fall through to the update path below */
        }
        else
        {
            static int wait_ticks = 0;
            wait_ticks++;

            /* Keep waiting for retained pc/event (up to 15s) before deciding */
            if (!g_pc_event_received && wait_ticks < 15)
            {
                return;
            }

            /* Timeout reached with or without pc-event: create layout */
            if (wait_ticks >= 5)
            {
                RTK_LOGI("V3_UI", "timeout -> create layout %s (no data)\n", layout_get_name(g_layout_id));
                wait_ticks = 0;
                destroy_waiting_ui();
                layout_switch(g_layout_id);
            }
            /* else: wait_ticks < 5, pc-event received — fall through to data path */;
        }
    }

    /* FRD/line stall detection (immediate, outside throttle)
     * Warns if LCDC interrupts appear to have stopped */
    {
        static uint32_t s_last_frd_warn_tick = 0;
        uint32_t        now                  = rtos_time_get_current_system_time_ms();
        uint32_t        frd_age              = now - lcdc_core_get_last_frd_tick();
        /* Skip until first FRD fires (last_frd_tick starts at 0 → false stall alarm) */
        if (lcdc_core_get_frd_count() > 0 &&
            (frd_age > 1000) && ((now - s_last_frd_warn_tick) >= 10000))
        {
            s_last_frd_warn_tick = now;
            RTK_LOGI("V3_UI",
                     "DIAG: FRD stall detected! age=%lu ms flip=%lu line=0x%lx ovr=%lu\n",
                     (unsigned long) frd_age,
                     (unsigned long) lcdc_core_get_flip_count(),
                     (unsigned long) lcdc_core_get_last_line_tick(),
                     (unsigned long) lcdc_core_get_pend_overwrite());
        }
    }

    /* Atomically check and clear flag — prevents race with MQTT writer (pc_dashboard.c)
     * which sets g_new_data_ready = true inside taskENTER_CRITICAL(). */
    taskENTER_CRITICAL();
    bool new_data    = g_new_data_ready;
    g_new_data_ready = false;
    taskEXIT_CRITICAL();

    if (!new_data)
    {
        return;
    }

    /* First data: transition from waiting screen to dashboard */
    if (!layout_is_created())
    {
        RTK_LOGI("V3_UI", "first data -> create layout %s\n", layout_get_name(g_layout_id));
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
