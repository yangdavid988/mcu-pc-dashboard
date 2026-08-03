/*
 * Copyright (c) 2025 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hal/backlight_ctrl.h"
#include "config/threshold_config.h"
#include "string.h"
#include "ameba_soc.h"
#include "pwmout_api.h" /* PC_1 / PB_3 PinName constants (no pwmout API calls) */
#include "log.h"

#ifndef TAG
#define TAG "BACKLIGHT"
#endif

/* ========================================================================
 * Backlight brightness control — TIM4 raw-register PWM (~1 kHz)
 *
 * Pin assignment:
 *   DBL070 (ST7277)   -> _PC_1
 *   ST7262            -> _PB_3  (_PA_17 is DISP, not backlight)
 *
 * APB=40 MHz, PSC=0, ARR=39999 -> ~1 kHz.
 * At this frequency the MOSFET gate RC filter smooths the PWM into a
 * continuous voltage, giving linear brightness control.
 * ======================================================================== */

#define BL_TIMER_IDX 4 /* TIM4 */
#define BL_PWM_CHAN  0

/* Timer frequency calculation:
 *   APB 40 MHz / (PSC+1) / (ARR+1) = 40 MHz / 1 / 40000 = 1 kHz
 *   CCRx = duty_ratio * BL_PERIOD_TICKS
 *   ARR max = 65535 (16-bit) -> min freq ~610 Hz at PSC=0. */
#define BL_PRESCALER    0
#define BL_PERIOD_TICKS 40000                 /* full-scale compare value = ARR + 1 */
#define BL_ARR          (BL_PERIOD_TICKS - 1) /* counter reload */

static bool    g_bl_initialized = false;
static int     g_user_pct       = 100; /* user-space brightness (with remap applied internally) */
static int     g_restore_pct    = 100; /* brightness saved before entering standby */
static PinName g_bl_pin;               /* backlight PinName (for pinmux) */
static u32     g_bl_gpio_pin;          /* backlight GPIO pin number (for GPIO ops) */

/*
 * Brightness remap: user_percent(0..100) -> actual PWM duty(0.0 .. 1.0)
 *
 * The DBL070 backlight circuit uses a MOSFET gate RC filter to smooth PWM
 * into a DC level.  The MOSFET saturates above ~3-5 % PWM duty -- the gate
 * never discharges enough to dim the LED.  A linear mapping would waste
 * >90 % of the user range in the saturated zone.
 *
 * Cubic curve (gamma ~ 3.0) spreads the sensitive low-PWM region across
 * more user steps:
 *   duty = (user / 100)^3
 *
 *   user  0 -> duty 0 %     (off)
 *   user 10 -> duty 0.1 %   (just visible)
 *   user 30 -> duty 2.7 %   (dim)
 *   user 50 -> duty 12.5 %  (mid)
 *   user 80 -> duty 51.2 %  (bright)
 *   user 100 -> duty 100 %  (full)
 */
static float backlight_remap(int user_pct)
{
    float x = (float) user_pct / 100.0f;
    return x * x * x; /* cubic -- no math.h needed */
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
        /* Entering standby: save current brightness, dim to standby level */
        g_restore_pct = g_user_pct;
        backlight_set((int) BRIGHTNESS_STANDBY_PCT);

        RTK_LOGI(TAG, "backlight_set_standby(1) -> %d%% (saved %d%%)\n", (int) BRIGHTNESS_STANDBY_PCT, g_restore_pct);
    }
    else
    {
        /* Exiting standby: restore user's pre-standby brightness */
        backlight_set(g_restore_pct);

        RTK_LOGI(TAG, "backlight_set_standby(0) -> restore %d%%\n", g_restore_pct);
    }
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
