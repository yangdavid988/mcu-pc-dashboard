#include "backlight_ctrl.h"
#include "threshold_config.h"
#include "string.h"
#include "ameba_soc.h"
#include "pwmout_api.h"
#include "pwmout_ex_api.h"
#include "log.h"

#ifndef TAG
#define TAG "BACKLIGHT"
#endif

/* ========================================================================
 * Backlight brightness control — mbed hardware PWM (both platforms)
 *
 * Uses TIM4 channel 0 with the mbed pwmout API to drive the backlight
 * MOSFET gate.  Pinmux is handled by pwmout_init() internally.
 *
 * Pin assignment:
 *   DBL070 (ST7277)  backlight on _PC_1
 *   ST7262           backlight on _PB_3  (_PA_17 is DISP, not backlight)
 *
 * Timer: 100 Hz PWM via TIM4, zero CPU overhead (hardware output).
 * ======================================================================== */

static pwmout_t g_bl_pwm;
static bool     g_bl_initialized = false;
static int      g_user_pct       = 100; /* user-space brightness (with remap applied internally) */

/*
 * Brightness remap: user_percent(0..100) → actual PWM duty(0.25 .. 1.0)
 *
 * The DBL070 backlight circuit has a MOSFET gate RC filter that smooths PWM
 * into a DC level.  At 100 Hz the MOSFET switches fully, but the LED backlight
 * itself needs ~25 % PWM duty to emit visible light (LED minimum current
 * threshold).  A linear user → PWM mapping would waste the 0–25 % range.
 *
 * This remap gives:
 *   user  0 % → PWM 25 %  (just barely visible)
 *   user 50 % → PWM 62.5 %
 *   user 100 % → PWM 100 %
 */
static float backlight_remap(int user_pct)
{
    return 0.25f + 0.75f * (float) user_pct / 100.0f;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void backlight_init(void)
{
    if (g_bl_initialized)
        return;

    PinName bl_pin =
#ifdef USE_DBL070
        PC_1;
#else
        PB_3; /* _PA_17 is DISP, not backlight */
#endif

    RTK_LOGI(TAG, "backlight_init: mbed PWM on %s\n", bl_pin == PC_1 ? "_PC_1" : "_PB_3");

    memset(&g_bl_pwm, 0, sizeof(g_bl_pwm));
    g_bl_pwm.pwmtimer_idx = 4; /* TIM4 */
    g_bl_pwm.pwm_idx      = 0; /* channel 0 */

    pwmout_init(&g_bl_pwm, bl_pin);

    {
        u32 func = Pinmux_ConfigGet((u8) bl_pin);
        RTK_LOGI(TAG, "pinmux: func=%d (expect TIM4_PWM0=%d)\n", (int) func, (int) PINMUX_FUNCTION_TIM4_PWM0);
    }

    pwmout_period_us(&g_bl_pwm, 10000); /* 100 Hz — low freq to let MOSFET gate cap fully discharge */
    pwmout_write(&g_bl_pwm, 1.0f);      /* 100 % brightness */
    pwmout_start(&g_bl_pwm);

    g_bl_initialized = true;

    RTK_LOGI(TAG, "backlight_init: done (en=%d normal=%d%% standby=%d%%)\n", (int) BRIGHTNESS_ENABLED, (int) BRIGHTNESS_NORMAL_PCT, (int) BRIGHTNESS_STANDBY_PCT);
}

void backlight_set_standby(bool active)
{
    if (!BRIGHTNESS_ENABLED)
    {
        RTK_LOGI(TAG, "backlight_set_standby(%d) ignored (disabled)\n", (int) active);
        return;
    }

    int pct = active ? (int) BRIGHTNESS_STANDBY_PCT : (int) BRIGHTNESS_NORMAL_PCT;
    backlight_set(pct); /* goes through remap */

    RTK_LOGI(TAG, "backlight_set_standby(%d) -> %d%%\n", (int) active, pct);
}

void backlight_set(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    g_user_pct = percent;

    float f = backlight_remap(percent);
    pwmout_write(&g_bl_pwm, f);

    RTK_LOGI(TAG, "backlight_set: %d%% (pwm=%d%%)\n", percent, (int) (f * 100.0f + 0.5f));
}

void backlight_adjust(int delta)
{
    int cur     = backlight_get();
    int new_val = cur + delta;

    /* Clamp to [BL_MIN_PCT, 100] */
    if (new_val < BL_MIN_PCT)
        new_val = BL_MIN_PCT;
    if (new_val > 100)
        new_val = 100;

    backlight_set(new_val);
}

int backlight_get(void)
{
    return g_user_pct;
}
