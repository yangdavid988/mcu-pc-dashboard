/*
 * Copyright (c) 2025 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "st7262_cfg.h"
#include "ameba_soc.h"

/* ========================================================================
 * ST7262 pin configuration
 * ======================================================================== */
static void st7262_pinmux(void)
{
    RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "%s \r\n", __func__);

    GPIO_InitTypeDef GPIO_InitStruct;

    GPIO_InitStruct.GPIO_Pin = _PA_17;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&GPIO_InitStruct);
    GPIO_WriteBit(_PA_17, 1);

    GPIO_InitStruct.GPIO_Pin = _PB_3;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&GPIO_InitStruct);
    GPIO_WriteBit(_PB_3, 1);

    Pinmux_Config(_PB_15, PINMUX_FUNCTION_LCD_D0);
    Pinmux_Config(_PB_17, PINMUX_FUNCTION_LCD_D1);
    Pinmux_Config(_PB_21, PINMUX_FUNCTION_LCD_D2);
    Pinmux_Config(_PB_18, PINMUX_FUNCTION_LCD_D3);
    Pinmux_Config(_PA_6,  PINMUX_FUNCTION_LCD_D4);
    Pinmux_Config(_PA_8,  PINMUX_FUNCTION_LCD_D5);
    Pinmux_Config(_PA_7,  PINMUX_FUNCTION_LCD_D6);
    Pinmux_Config(_PA_10, PINMUX_FUNCTION_LCD_D7);

    Pinmux_Config(_PB_9,  PINMUX_FUNCTION_LCD_D8);
    Pinmux_Config(_PB_11, PINMUX_FUNCTION_LCD_D9);
    Pinmux_Config(_PB_10, PINMUX_FUNCTION_LCD_D10);
    Pinmux_Config(_PB_16, PINMUX_FUNCTION_LCD_D11);
    Pinmux_Config(_PB_22, PINMUX_FUNCTION_LCD_D12);
    Pinmux_Config(_PB_23, PINMUX_FUNCTION_LCD_D13);
    Pinmux_Config(_PB_14, PINMUX_FUNCTION_LCD_D14);
    Pinmux_Config(_PB_12, PINMUX_FUNCTION_LCD_D15);

    Pinmux_Config(_PA_22, PINMUX_FUNCTION_LCD_D16);
    Pinmux_Config(_PA_25, PINMUX_FUNCTION_LCD_D17);
    Pinmux_Config(_PA_29, PINMUX_FUNCTION_LCD_D18);
    Pinmux_Config(_PB_4,  PINMUX_FUNCTION_LCD_D19);
    Pinmux_Config(_PB_5,  PINMUX_FUNCTION_LCD_D20);
    Pinmux_Config(_PB_6,  PINMUX_FUNCTION_LCD_D21);
    Pinmux_Config(_PB_7,  PINMUX_FUNCTION_LCD_D22);
    Pinmux_Config(_PB_8,  PINMUX_FUNCTION_LCD_D23);

    Pinmux_Config(_PA_16, PINMUX_FUNCTION_LCD_RGB_HSYNC);
    Pinmux_Config(_PA_13, PINMUX_FUNCTION_LCD_RGB_VSYNC);
    Pinmux_Config(_PA_9,  PINMUX_FUNCTION_LCD_RGB_DCLK);
    Pinmux_Config(_PA_14, PINMUX_FUNCTION_LCD_RGB_DE);
}

/* ========================================================================
 * ST7262 configuration table
 * ======================================================================== */
const lcdc_screen_cfg_t g_st7262_cfg = {
    .vsw            = 1,
    .vbp            = 4,
    .vfp            = 6,
    .hsw            = 4,
    .hbp            = 40,
    .hfp            = 40,
    .image_format   = LDC_IMG_FMT_ARGB8888,
    .pinmux_config  = st7262_pinmux,
    .backlight_init = NULL,
    .name           = "ST7262",
    .fb_base        = 0x60000000,
};
