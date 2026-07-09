#include "pc_dashboard_theme.h"
#include "pc_dashboard_layout.h"
#include "gpio_irq_api.h"
#include "log.h"

#ifndef TAG
#define TAG "V3_GPIO"
#endif

/* ===== Debounce time window ===== */
#define GPIO_DEBOUNCE_MS        250

/*
 * ISR-safe timestamp: use SDK wrapper which internally selects
 * xTaskGetTickCountFromISR() in ISR context, xTaskGetTickCount() in task.
 * DO NOT call xTaskGetTickCount() directly in ISR (may use taskENTER_CRITICAL).
 */
static inline uint32_t isr_safe_tick_ms(void)
{
    return rtos_time_get_current_system_time_ms();
}

/* ===== GPIO interrupt objects ===== */
static gpio_irq_t gpio_layout;
static gpio_irq_t gpio_theme;

/* ===== Deferred switch flags (ISR sets flags, LVGL timer processes them) ===== */
static volatile bool g_pending_layout_switch = false;
static volatile bool g_pending_theme_switch = false;

/* ===== ISR debounce timestamps (volatile: written in ISR, read in ISR) ===== */
static volatile uint32_t g_last_layout_ms = 0;
static volatile uint32_t g_last_theme_ms = 0;

/* ===== Layout switch ISR ===== */
static void layout_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;

    uint32_t now = isr_safe_tick_ms();

    /* Debounce: reject re-trigger within 250ms */
    if (now - g_last_layout_ms < GPIO_DEBOUNCE_MS)
        return;
    g_last_layout_ms = now;

    g_pending_layout_switch = true;
}

/* ===== Theme switch ISR ===== */
static void theme_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;

    uint32_t now = isr_safe_tick_ms();

    /* Debounce: reject re-trigger within 250ms */
    if (now - g_last_theme_ms < GPIO_DEBOUNCE_MS)
        return;
    g_last_theme_ms = now;

    g_pending_theme_switch = true;
}

void gpio_control_process(void)
{
    /* Process deferred layout switch (single-shot: clear flag, process once) */
    if (g_pending_layout_switch && layout_is_created())
    {
        g_pending_layout_switch = false;
        layout_id_t next_id = (g_layout_id + 1) % LAYOUT_MAX;
        RTK_LOGI(TAG, "DEFERRED layout_switch -> %s\n",
            layout_get_name(next_id));
        layout_switch(next_id);
    }

    /* Process deferred theme switch (single-shot) */
    if (g_pending_theme_switch && layout_is_created())
    {
        g_pending_theme_switch = false;
        theme_id_t next_id = (g_theme_id + 1) % THEME_MAX;
        RTK_LOGI(TAG, "DEFERRED theme_switch -> %s\n",
            theme_get_name(next_id));
        theme_switch(next_id);
    }
}

void gpio_control_init(void)
{
#ifdef USE_DBL070
    /* ---- DBL070: PB16 = Layout, PB14 = Theme ---- */
    gpio_irq_init(&gpio_layout, _PB_16, layout_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_layout, PullUp);
    gpio_irq_set(&gpio_layout, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_layout);

    gpio_irq_init(&gpio_theme, _PB_14, theme_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_theme, PullUp);
    gpio_irq_set(&gpio_theme, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_theme);
#else
    /* ---- ST7262: PB0 = Layout, PA31 = Theme ---- */
    gpio_irq_init(&gpio_layout, _PB_0, layout_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_layout, PullDown);
    gpio_irq_set(&gpio_layout, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_layout);

    gpio_irq_init(&gpio_theme, _PA_31, theme_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_theme, PullDown);
    gpio_irq_set(&gpio_theme, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_theme);
#endif
}
