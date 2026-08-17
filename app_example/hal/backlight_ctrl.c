/*
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * ameba_soc.h MUST come before backlight_ctrl.h:
 *   - ameba_soc.h → platform_autoconf.h → #define CONFIG_SCREEN_DBL070 (or not)
 *   - backlight_ctrl.h → threshold_config.h → #ifdef CONFIG_SCREEN_DBL070
 *   If we included backlight_ctrl.h first, CONFIG_SCREEN_DBL070 would still be
 *   undefined when threshold_config.h is processed, causing BRIGHTNESS_STANDBY_PCT
 *   and BL_MIN_PCT to fall through to the ST7262 defaults regardless of Kconfig.
 */
#include "ameba_soc.h"
#include "hal/backlight_ctrl.h"
#include "config/threshold_config.h"
#include "string.h"
#include "pwmout_api.h"
#include "log.h"

#ifndef TAG
#define TAG "BACKLIGHT"
#endif

/* ========================================================================
 * Backlight brightness control — raw register PWM (both platforms)
 *
 * Uses TIM4 channel 0 with direct register access to drive the backlight
 * MOSFET gate.
 *
 * Pin assignment:
 *   DBL070 (ST7277)  backlight on _PC_1
 *   ST7262           backlight on _PB_3  (_PA_17 is DISP, not backlight)
 *
 * Timer: ~1 kHz PWM via TIM4, zero CPU overhead (hardware output).
 *
 * Reference design: pwm_raw_RGB_demo.c — channels configured BEFORE timer
 * start, full CCxInit (preload, polarity, output mode, compare value),
 * explicit prescaler in TimeBaseInit struct.
 *
 * Frequency note: the backlight MOSFET gate has an RC filter.  At low PWM
 * frequencies (~100 Hz) the gate fully discharges during the long OFF period,
 * so the MOSFET switches fully OFF each cycle and the LED is OFF except at
 * near-100 % duty.  At ~1 kHz (no prescaler) the RC filter smooths the PWM,
 * keeping the gate voltage above the MOSFET threshold across a wide duty
 * range — the MOSFET acts as a variable resistor, giving continuous
 * brightness control.
 * ======================================================================== */

#define BL_TIMER_IDX 4 /* TIM4 */
#define BL_PWM_CHAN  0 /* channel 0 */

/*
 * Timer frequency calculation (no prescaler):
 *   APB clock   = 40 MHz
 *   Prescaler   = 0       -> timer clock = 40 / 1 = 40 MHz
 *   Period      = 40000   -> PWM freq    = 40 MHz / 40000 = 1 kHz
 *   CCRx value  = duty_ratio * BL_PERIOD_TICKS
 *
 * The 16-bit ARR register limits the max period to 65535 ticks.
 * With PSC=0 the minimum PWM frequency is ~610 Hz (ARR=65535).
 * 1 kHz is a good balance: fast enough for RC smoothing, slow enough
 * for decent CCRx resolution (16-bit).
 */
#define BL_PRESCALER    0
#define BL_PERIOD_TICKS 40000                 /* full-scale compare value = ARR + 1 */
#define BL_ARR          (BL_PERIOD_TICKS - 1) /* counter reload */

static bool    g_bl_initialized = false;
static int     g_user_pct       = 100; /* user-space brightness (with remap applied internally) */
static int     g_restore_pct    = 100; /* brightness saved before entering standby */
static PinName g_bl_pin;               /* backlight PinName (for pinmux) */
static u32     g_bl_gpio_pin;          /* backlight GPIO pin number (for GPIO ops) */

/* ---- Gradual fade state (duty-domain linear) ---- */
static bool     g_fade_pending   = false; /* true during the initial delay */
static bool     g_fade_active    = false; /* true while fade is in progress */
static int      g_fade_start_pct = 100;   /* brightness (%) when fade began */
static int      g_fade_target    = 3;     /* target brightness (%) */
static uint32_t g_fade_start_ms  = 0;     /* timestamp when pending/active began */
#define BL_FADE_INIT_DELAY_MS 2100        /* delay before first step (sweep ~2s) */
#define BL_FADE_DURATION_MS   8000        /* total fade time (ms), duty changes linearly */

/*
 * Brightness remap: user_percent(0..100) -> actual PWM duty(0.0 .. 1.0)
 *
 * Panel-specific curves — each backlight circuit has different
 * voltage-to-brightness transfer characteristics.
 *
 * DBL070 (ST7277): MOSFET gate RC filter.  The MOSFET saturates above
 *   ~3-5 % PWM duty, so low-end needs fine resolution.  Cubic spreads
 *   the sensitive low-PWM region across more user steps:
 *     user  10 -> duty 0.1 %   (just visible)
 *     user  30 -> duty 2.7 %   (near saturation)
 *     user 100 -> duty 100 %
 *
 * ST7262: backlight driven directly by PWM, no MOSFET saturation.
 *   Quadratic provides a gentler roll-off than cubic, giving usable
 *   output at low user-percentages without being too abrupt:
 *     user  20 -> duty 4.0 %   (dim but visible)
 *     user  50 -> duty 25 %
 *     user 100 -> duty 100 %
 */
static float backlight_remap(int user_pct)
{
#ifdef CONFIG_SCREEN_DBL070
    float x = (float) user_pct / 100.0f;
    return x * x * x; /* cubic — MOSFET saturation compensation */
#elif defined(CONFIG_SCREEN_ST7262)
    /* ST7262 */
    float x = (float) user_pct / 100.0f;
    return x * x; /* quadratic — gentle low-end, no MOSFET */
#endif
}

#ifdef CONFIG_SCREEN_ST7262
/*
 * Square root (ST7262 inverse remap).
 * Newton-Raphson iteration, no math.h needed.
 */
static float _sqrt(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;

    /* Scale x to [0.25, 1) for fast convergence */
    float scale = 1.0f;
    while (x < 0.25f)
    {
        x *= 4.0f;
        scale *= 0.5f;
    }

    float y = x;
    for (int i = 0; i < 4; i++)
        y = (y + x / y) * 0.5f;

    return y * scale;
}
#endif /* CONFIG_SCREEN_ST7262 */

#ifdef CONFIG_SCREEN_DBL070
/*
 * Cube root (DBL070 inverse remap).
 * Newton-Raphson iteration, no math.h needed.
 */
static float _cbrt(float x)
{
    if (x <= 0.0f)
        return 0.0f;
    if (x >= 1.0f)
        return 1.0f;

    /*
     * Scale x into [0.125, 1) where Newton-Raphson converges
     * reliably.  Each x8 corresponds to /2 in the result.
     *
     * Without scaling, small inputs (e.g. x = 0.008) start from
     * y = x = 0.008 which is ~25x away from the true root 0.2;
     * the first iteration overshoots to ~41 and subsequent steps
     * crawl back toward zero instead of the correct answer.
     */
    float scale = 1.0f;
    while (x < 0.125f)
    {
        x *= 8.0f;
        scale *= 0.5f;
    }

    float y = x;
    for (int i = 0; i < 7; i++)
        y = (2.0f * y + x / (y * y)) / 3.0f;

    return y * scale;
}
#endif /* CONFIG_SCREEN_DBL070 */

/* ========================================================================
 * Public API
 * ======================================================================== */

void backlight_init(void)
{
    if (g_bl_initialized)
        return;

    PinName bl_pin =
#ifdef CONFIG_SCREEN_DBL070
        PC_1;
#elif defined(CONFIG_SCREEN_ST7262)
        PB_3; /* _PA_17 is DISP, not backlight */
#endif

    g_bl_pin      = bl_pin;
    g_bl_gpio_pin = (bl_pin == PC_1) ? (u32) _PC_1 : (u32) _PB_3;

    RTK_LOGI(TAG, "backlight_init: raw PWM on %s\n", bl_pin == PC_1 ? "_PC_1" : "_PB_3");

    /* ---- 1. Enable TIM4 peripheral clock ---- */
    RCC_PeriphClockCmd(APBPeriph_TIMx[BL_TIMER_IDX],
                       APBPeriph_TIMx_CLOCK[BL_TIMER_IDX],
                       ENABLE);

    /* ---- 2. Configure timer base (explicit prescaler + period) ---- */
    {
        RTIM_TimeBaseInitTypeDef TIM_InitStruct;
        RTIM_TimeBaseStructInit(&TIM_InitStruct);
        TIM_InitStruct.TIM_Idx       = BL_TIMER_IDX;
        TIM_InitStruct.TIM_Prescaler = BL_PRESCALER;
        TIM_InitStruct.TIM_Period    = BL_ARR;
        RTIM_TimeBaseInit(TIMx[BL_TIMER_IDX], &TIM_InitStruct, TIMx_irq[BL_TIMER_IDX], NULL, NULL);
    }

    /* ---- 3. Configure PWM channel (complete CCx init) ----
     * Key differences from the mbed pwmout API:
     *   a) RTIM_CCxInit() sets OCxPE (preload), OCxM (output mode),
     *      polarity, and initial compare value -- all in one shot.
     *   b) TIM_OCPreload_Disable makes CCRx changes take effect
     *      immediately (no wait for next update event).
     *   c) Channel is configured BEFORE the timer counter starts.
     */
    {
        TIM_CCInitTypeDef TIM_CCInitStruct;
        RTIM_CCStructInit(&TIM_CCInitStruct);
        TIM_CCInitStruct.TIM_CCMode       = TIM_CCMode_PWM;
        TIM_CCInitStruct.TIM_OCPulse      = BL_PERIOD_TICKS; /* 100 % initially */
        TIM_CCInitStruct.TIM_OCProtection = TIM_OCPreload_Disable;
        TIM_CCInitStruct.TIM_CCPolarity   = TIM_CCPolarity_High;
        RTIM_CCxInit(TIMx[BL_TIMER_IDX], &TIM_CCInitStruct, BL_PWM_CHAN);
    }

    /* ---- 4. Enable channel output ---- */
    RTIM_CCxCmd(TIMx[BL_TIMER_IDX], BL_PWM_CHAN, TIM_CCx_Enable);

    /* ---- 5. Route pin to TIM4 PWM function ---- */
    Pinmux_Config(bl_pin, PINMUX_FUNCTION_TIM4_PWM0);

    /* ---- 6. Start timer LAST (counter begins after channel is fully set up) ---- */
    RTIM_Cmd(TIMx[BL_TIMER_IDX], ENABLE);

    g_bl_initialized = true;

    /*
     * Set initial brightness to the configured normal level.
     * RTIM_CCxInit (step 3) already set compare=100%, so the display is
     * briefly at full brightness before settling to the user's default.
     */
    backlight_set((int) BRIGHTNESS_NORMAL_PCT);

    RTK_LOGI(TAG, "backlight_init: done (PSC=%d ARR=%d en=%d normal=%d%% standby=%d%%)\n", BL_PRESCALER, BL_ARR, (int) BRIGHTNESS_ENABLED, (int) BRIGHTNESS_NORMAL_PCT, (int) BRIGHTNESS_STANDBY_PCT);
}

void backlight_set_standby(bool active)
{
    if (!BRIGHTNESS_ENABLED)
    {
        RTK_LOGI(TAG, "backlight_set_standby(%d) ignored (disabled)\n", (int) active);
        return;
    }

    if (active)
    {
        /*
         * Entering standby: record brightness, defer fade start.
         * The actual fade begins after BL_FADE_INIT_DELAY_MS (~2 s)
         * to let the clock sweep animation complete without FPS
         * interference.  The fade then ramps duty linearly over
         * BL_FADE_DURATION_MS for a smooth, step-less transition.
         *
         * g_fade_start_pct mirrors the current brightness so the fade
         * transitions smoothly from the monitor level down to the standby
         * threshold — no "brighten then dim" glitch.  g_restore_pct
         * preserves the pre-standby brightness for restore on unlock.
         */
        g_restore_pct    = g_user_pct;
        g_fade_start_pct = g_user_pct;
        g_fade_target    = (int) BRIGHTNESS_STANDBY_PCT;
        g_fade_pending   = (g_fade_start_pct != g_fade_target);
        g_fade_active    = false;
        g_fade_start_ms  = rtos_time_get_current_system_time_ms();

        RTK_LOGI(TAG, "backlight_set_standby(1) -> fade %d%% -> %d%% (delay %d ms)\n", g_fade_start_pct, g_fade_target, BL_FADE_INIT_DELAY_MS);
    }
    else
    {
        /*
         * Exiting standby: stop any active fade immediately,
         * restore the pre-standby brightness.
         */
        g_fade_pending = false;
        g_fade_active  = false;
        backlight_set(g_restore_pct);

        RTK_LOGI(TAG, "backlight_set_standby(0) -> restore %d%%\n", g_restore_pct);
    }
}

void backlight_fade_tick(void)
{
    uint32_t now = rtos_time_get_current_system_time_ms();

    /* ---- Pending: initial delay before fade begins ---- */
    if (g_fade_pending)
    {
        if ((now - g_fade_start_ms) >= BL_FADE_INIT_DELAY_MS)
        {
            /* Start the actual fade: reset timer for duty progression */
            g_fade_pending  = false;
            g_fade_active   = true;
            g_fade_start_ms = now;
            RTK_LOGI(TAG, "backlight_fade: start (cur=%d%% tgt=%d%% dur=%d ms)\n", g_fade_start_pct, g_fade_target, BL_FADE_DURATION_MS);
        }
        return;
    }

    if (!g_fade_active)
        return;

    /* ---- Active: duty-domain linear progression ---- *
     * Compute elapsed fraction of total duration, then
     * linearly interpolate in the duty domain for a smooth
     * step-less visual transition.                        */
    uint32_t elapsed    = now - g_fade_start_ms;
    float    t          = (float) elapsed / (float) BL_FADE_DURATION_MS;
    float    start_duty = backlight_remap(g_fade_start_pct);
    float    end_duty   = backlight_remap(g_fade_target);
    float    duty;

    if (t >= 1.0f)
    {
        backlight_set(g_fade_target);
        g_fade_active = false;
        RTK_LOGI(TAG, "backlight_fade: done (%d%%)\n", g_fade_target);
        return;
    }

    duty = start_duty + (end_duty - start_duty) * t;

    /* Inverse remap: duty -> user percentage */
    int user_pct;
#ifdef CONFIG_SCREEN_DBL070
    user_pct = (int) (_cbrt(duty) * 100.0f + 0.5f);
#elif defined(CONFIG_SCREEN_ST7262)
    /* ST7262: quadratic inverse = sqrt */
    user_pct = (int) (_sqrt(duty) * 100.0f + 0.5f);
#endif
    if (user_pct > 100)
        user_pct = 100;
    if (user_pct < 0)
        user_pct = 0;

    backlight_set(user_pct);
}

void backlight_set(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    g_user_pct = percent;

    float f    = backlight_remap(percent);
    u32   ccrx = (u32) (f * (float) BL_PERIOD_TICKS);

    if (ccrx == 0)
    {
        /*
         * 0 % brightness: switch pin to GPIO and drive LOW.
         *
         * Writing CCRx compare = 0 (or even a tiny non-zero like 1)
         * corrupts the timer state machine — subsequent non-zero
         * writes produce 100 % output regardless of value.
         *
         * Bypass the PWM entirely: route the pin to GPIO, drive it
         * LOW so the MOSFET gate discharges and the LED turns off.
         * The PWM timer keeps running in the background, so when we
         * switch back the compare value is already loaded and the
         * output appears immediately at the correct duty cycle.
         */
        Pinmux_Config(g_bl_pin, PINMUX_FUNCTION_GPIO);
        {
            GPIO_InitTypeDef gi;
            memset(&gi, 0, sizeof(gi));
            gi.GPIO_Pin  = g_bl_gpio_pin;
            gi.GPIO_Mode = GPIO_Mode_OUT;
            GPIO_Init(&gi);
            GPIO_WriteBit(g_bl_gpio_pin, 0);
        }
    }
    else
    {
        /*
         * Normal (non-zero) brightness: load the compare value
         * first, then restore pinmux to TIM4 PWM.
         *
         * Order matters: compare must be written BEFORE the pin
         * switches back to PWM, so the correct duty appears on
         * the pin from the first cycle.
         */
        RTIM_CCRxSet(TIMx[BL_TIMER_IDX], ccrx, BL_PWM_CHAN);
        Pinmux_Config(g_bl_pin, PINMUX_FUNCTION_TIM4_PWM0);
    }
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
