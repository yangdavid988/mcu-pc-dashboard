#include "core/standby_manager.h"
#include "core/pc_dashboard.h"   /* g_screen_state, g_pc_event_received,
                                      g_new_data_ready, ScreenState_t        */
#include "hal/gpio_control.h"   /* gpio_control_suspend_ui_buttons / resume */
#include "hal/backlight_ctrl.h" /* backlight_set_standby                   */
#include "log.h"

#ifndef TAG
#define TAG "STANDBY"
#endif

/* ========================================================================
 * Standby Manager — implementation
 *
 * Single entry/exit point for all non-UI standby operations called from the
 * MQTT task context (via parse_lock_event → standby_enter / standby_exit).
 *
 * UI operations (clock face creation, fade animation, backlight restore on
 * exit) remain in the LVGL timer callback — they require LVGL task context
 * and cannot be mixed with MQTT task calls.
 * ======================================================================== */

void standby_enter(void)
{
    taskENTER_CRITICAL();
    g_screen_state      = SCREEN_STATE_CLOCK;
    g_pc_event_received = true;
    taskEXIT_CRITICAL();

    /* Dim backlight immediately on lock — visual feedback is instant
     * even though the clock face UI takes one LVGL timer tick (~16 ms)
     * to appear. GPIO layout/theme buttons are hardware-disabled to
     * eliminate ISR overhead during standby. */
    backlight_set_standby(true);
    gpio_control_suspend_ui_buttons();

    RTK_LOGI(TAG, "Standby entered (backlight dim, GPIO IRQs suspended)\n");
}

void standby_exit(void)
{
    taskENTER_CRITICAL();
    g_screen_state      = SCREEN_STATE_MONITOR;
    g_pc_event_received = true;
    g_new_data_ready    = true;
    taskEXIT_CRITICAL();

    /* Resume GPIO first so layout/theme buttons are live as soon as the
     * monitor UI appears. Backlight is NOT restored here — it fires from
     * the LVGL fade-completion callback (pc_dashboard_lock_screen.c) to
     * avoid a bright clock-face flash before the fade-out transition. */
    gpio_control_resume_ui_buttons();

    RTK_LOGI(TAG, "Standby exited (GPIO IRQs resumed, backlight restore deferred)\n");
}

bool standby_is_active(void)
{
    return (g_screen_state == SCREEN_STATE_CLOCK);
}
