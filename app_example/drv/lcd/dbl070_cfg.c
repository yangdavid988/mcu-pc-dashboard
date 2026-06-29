/*
 * Copyright (c) 2025 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dbl070_cfg.h"
#include "ameba_soc.h"

/* ========================================================================
 * DBL070 pin configuration
 * ======================================================================== */
static void dbl070_pinmux(void)
{
    RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "%s \r\n", __func__);

    /* Reset */
    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Pin = _PC_0;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&gpio_init);
    GPIO_WriteBit(_PC_0, 1);
    DelayMs(11);

    /* Display enable */
    gpio_init.GPIO_Pin = _PB_31;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&gpio_init);
    GPIO_WriteBit(_PB_31, 1);
    DelayMs(125);

    /* Backlight */
    DelayMs(100);
    gpio_init.GPIO_Pin = _PC_1;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&gpio_init);
    GPIO_WriteBit(_PC_1, 1);

    /* RGB data lines */
    Pinmux_Config(_PA_20, PINMUX_FUNCTION_LCD_D0);
    Pinmux_Config(_PA_21, PINMUX_FUNCTION_LCD_D1);
    Pinmux_Config(_PA_22, PINMUX_FUNCTION_LCD_D2);
    Pinmux_Config(_PA_23, PINMUX_FUNCTION_LCD_D3);
    Pinmux_Config(_PA_24, PINMUX_FUNCTION_LCD_D4);
    Pinmux_Config(_PA_25, PINMUX_FUNCTION_LCD_D5);
    Pinmux_Config(_PA_26, PINMUX_FUNCTION_LCD_D6);
    Pinmux_Config(_PA_27, PINMUX_FUNCTION_LCD_D7);

    Pinmux_Config(_PA_12, PINMUX_FUNCTION_LCD_D8);
    Pinmux_Config(_PA_13, PINMUX_FUNCTION_LCD_D9);
    Pinmux_Config(_PA_14, PINMUX_FUNCTION_LCD_D10);
    Pinmux_Config(_PA_15, PINMUX_FUNCTION_LCD_D11);
    Pinmux_Config(_PA_16, PINMUX_FUNCTION_LCD_D12);
    Pinmux_Config(_PA_17, PINMUX_FUNCTION_LCD_D13);
    Pinmux_Config(_PA_18, PINMUX_FUNCTION_LCD_D14);
    Pinmux_Config(_PA_19, PINMUX_FUNCTION_LCD_D15);

    Pinmux_Config(_PA_4,  PINMUX_FUNCTION_LCD_D16);
    Pinmux_Config(_PA_5,  PINMUX_FUNCTION_LCD_D17);
    Pinmux_Config(_PA_6,  PINMUX_FUNCTION_LCD_D18);
    Pinmux_Config(_PA_7,  PINMUX_FUNCTION_LCD_D19);
    Pinmux_Config(_PA_8,  PINMUX_FUNCTION_LCD_D20);
    Pinmux_Config(_PA_9,  PINMUX_FUNCTION_LCD_D21);
    Pinmux_Config(_PA_10, PINMUX_FUNCTION_LCD_D22);
    Pinmux_Config(_PA_11, PINMUX_FUNCTION_LCD_D23);

    /* Control lines */
    Pinmux_Config(_PB_28, PINMUX_FUNCTION_LCD_RGB_HSYNC);
    Pinmux_Config(_PB_29, PINMUX_FUNCTION_LCD_RGB_VSYNC);
    Pinmux_Config(_PA_28, PINMUX_FUNCTION_LCD_RGB_DCLK);
    Pinmux_Config(_PB_30, PINMUX_FUNCTION_LCD_RGB_DE);
}

/* ========================================================================
 * DBL070 configuration table
 * ======================================================================== */
const lcdc_screen_cfg_t g_dbl070_cfg = {
    .vsw            = 4,
    .vbp            = 16,
    .vfp            = 16,
    .hsw            = 4,
    .hbp            = 8,
    .hfp            = 8,
    .image_format   = LDC_IMG_FMT_ARGB8888,
    .pinmux_config  = dbl070_pinmux,
    .backlight_init = NULL,
    .name           = "DBL070",
    .fb_base        = 0x60000000,
};
