#include "t1720a_cfg.h"
#include "ameba_soc.h"
#include "hal/backlight_ctrl.h"

/* ========================================================================
 * T1720A pin configuration (from SDK panel_pin_config.c t1720a_800x480 entry)
 *
 * Data bus: 24-bit RGB (pins listed as D0..D23)
 * Ctrl:     DCLK=PB_13, DE=PA_17  (HSYNC/VSYNC=0xFFFFFFFF in SDK — LCDC still
 *           generates internal sync signals per timing params)
 * GPIO:     BL=PA_25, power_en=PA_23, reset=N/C
 * ======================================================================== */
static void t1720a_pinmux(void)
{
    RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "%s \r\n", __func__);

    /* ---- Power enable (active high) ---- */
    {
        GPIO_InitTypeDef gpio_init;
        gpio_init.GPIO_Pin  = _PA_23;
        gpio_init.GPIO_Mode = GPIO_Mode_OUT;
        GPIO_Init(&gpio_init);
        GPIO_WriteBit(_PA_23, 1);
    }

    /* ---- RGB data lines D0..D23 ---- */
    Pinmux_Config(_PB_31, PINMUX_FUNCTION_LCD_D0);
    Pinmux_Config(_PC_0,  PINMUX_FUNCTION_LCD_D1);
    Pinmux_Config(_PC_1,  PINMUX_FUNCTION_LCD_D2);
    Pinmux_Config(_PA_12, PINMUX_FUNCTION_LCD_D3);
    Pinmux_Config(_PA_13, PINMUX_FUNCTION_LCD_D4);
    Pinmux_Config(_PA_14, PINMUX_FUNCTION_LCD_D5);
    Pinmux_Config(_PA_15, PINMUX_FUNCTION_LCD_D6);
    Pinmux_Config(_PA_16, PINMUX_FUNCTION_LCD_D7);

    Pinmux_Config(_PB_23, PINMUX_FUNCTION_LCD_D8);
    Pinmux_Config(_PB_24, PINMUX_FUNCTION_LCD_D9);
    Pinmux_Config(_PB_25, PINMUX_FUNCTION_LCD_D10);
    Pinmux_Config(_PB_26, PINMUX_FUNCTION_LCD_D11);
    Pinmux_Config(_PB_27, PINMUX_FUNCTION_LCD_D12);
    Pinmux_Config(_PB_28, PINMUX_FUNCTION_LCD_D13);
    Pinmux_Config(_PB_29, PINMUX_FUNCTION_LCD_D14);
    Pinmux_Config(_PB_30, PINMUX_FUNCTION_LCD_D15);

    Pinmux_Config(_PB_14, PINMUX_FUNCTION_LCD_D16);
    Pinmux_Config(_PB_15, PINMUX_FUNCTION_LCD_D17);
    Pinmux_Config(_PB_16, PINMUX_FUNCTION_LCD_D18);
    Pinmux_Config(_PB_17, PINMUX_FUNCTION_LCD_D19);
    Pinmux_Config(_PB_18, PINMUX_FUNCTION_LCD_D20);
    Pinmux_Config(_PB_19, PINMUX_FUNCTION_LCD_D21);
    Pinmux_Config(_PB_21, PINMUX_FUNCTION_LCD_D22);
    Pinmux_Config(_PB_22, PINMUX_FUNCTION_LCD_D23);

    /* ---- Control lines (DE-only, DCLK+DE) ---- */
    Pinmux_Config(_PB_13, PINMUX_FUNCTION_LCD_RGB_DCLK);
    Pinmux_Config(_PA_17, PINMUX_FUNCTION_LCD_RGB_DE);
}

/* ========================================================================
 * T1720A configuration table
 *
 * Timing derived from SDK panel_t1720a.c:
 *   800x480  RGB888  60 Hz  DE-mode
 *   hbp=40  hfp=40  hsw=4
 *   vbp=4   vfp=6   vsw=1
 *
 * Backlight: shares the same PWM-based backlight_ctrl.c.
 * _PA_25 → TIM4_PWM0 (same as DBL070/_PC_1 and ST7262/_PB_3).
 * ======================================================================== */
const lcdc_screen_cfg_t g_t1720a_cfg = {
    .vsw            = 1,
    .vbp            = 4,
    .vfp            = 6,
    .hsw            = 4,
    .hbp            = 40,
    .hfp            = 40,
    .image_format   = LDC_IMG_FMT_ARGB8888,
    .pinmux_config  = t1720a_pinmux,
    .backlight_init = backlight_init,
    .name           = "T1720A",
    .fb_base        = 0x60000000,
};
