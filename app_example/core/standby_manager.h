#ifndef STANDBY_MANAGER_H
#define STANDBY_MANAGER_H

#include <stdbool.h>

/* ========================================================================
 * Standby Manager — centralized standby entry / exit
 *
 * Orchestrates all non-UI standby operations from a single point:
 *   - GPIO layout/theme IRQ suspend/resume
 *   - Backlight dim (entry) — restore (exit) deferred to LVGL fade callback
 *   - g_screen_state + ancillary flags
 *
 * UI transitions (clock face creation/destruction, fade animation) remain
 * in the LVGL timer callback (pc_dashboard_ui.c) — they require the LVGL
 * task context and cannot be called from the MQTT task.
 * ======================================================================== */

/** Enter standby (CLOCK) mode.
 *  Sets g_screen_state = CLOCK, dims backlight immediately, suspends GPIO
 *  layout/theme IRQs. Called from MQTT lock event handler (MQTT task). */
void standby_enter(void);

/** Exit standby (MONITOR) mode.
 *  Sets g_screen_state = MONITOR, resumes GPIO layout/theme IRQs.
 *  Backlight restore is intentionally NOT here — it fires from the LVGL
 *  fade-completion callback to avoid a bright clock-face flash before
 *  the fade-out transition. Called from MQTT unlock event handler. */
void standby_exit(void);

/** Returns true when the system is in standby (CLOCK) mode. */
bool standby_is_active(void);

#endif /* STANDBY_MANAGER_H */
