#ifndef BACKLIGHT_CTRL_H
#define BACKLIGHT_CTRL_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * Backlight brightness control
 *
 * Uses TIM4 channel 0 with mbed hardware PWM.
 *
 * Runtime brightness can be adjusted via backlight_adjust() — clamped
 * automatically to BL_MIN_PCT (10 %) .. 100 %.
 *
 * Compile-time defaults (threshold_config.h) serve as the initial value
 * only; runtime changes never modify the header.
 *
 * API:
 *   backlight_init()               — one-time init
 *   backlight_set_standby(on)      — true = dim to standby %, false = restore
 *   backlight_set(percent)         — direct ..100 override
 *   backlight_adjust(delta)        — step up/down, clamped to [BL_MIN_PCT, 100]
 *   backlight_get()                — query current %
 * ======================================================================== */

#define BL_MIN_PCT  10 /* lowest allowed brightness (hard floor) — 0 % may corrupt timer if written */
#define BL_STEP_PCT 10 /* step size for backlight_adjust() */

/** Init TIM4 hardware PWM backlight */
void backlight_init(void);

/** Enter/exit standby dimming (uses threshold_config.h defaults) */
void backlight_set_standby(bool active);

/** Direct brightness set (0..100), bypasses BRIGHTNESS_ENABLED gate */
void backlight_set(int percent);

/** Step adjust — adds delta (±BL_STEP_PCT typical), clamped to [BL_MIN_PCT, 100] */
void backlight_adjust(int delta);

/** Get current brightness level (0..100) */
int backlight_get(void);

#endif /* BACKLIGHT_CTRL_H */
