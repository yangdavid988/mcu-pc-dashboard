#include "pc_dashboard_theme.h"
#include "pc_dashboard_layout.h"
#include "backlight_ctrl.h"
#include "gpio_irq_api.h"
#include "log.h"

#ifndef TAG
#define TAG "V3_GPIO"
#endif

/* ===== Debounce / long-press time windows ===== */
#define GPIO_DEBOUNCE_MS        250
#define BRIGHTNESS_LONG_MS      2000    /* hold ≥ 2 s → jump to min / max */

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

/* ========================================================================
 * Brightness buttons — pin mapping depends on platform
 * ======================================================================== */

/* DBL070: PB_15 = up, PB_17 = down
 * ST7262: PA_21 = up, PA_27 = down */
#ifdef USE_DBL070
#  define BL_UP_PIN     _PB_15
#  define BL_DOWN_PIN   _PB_17
#else
#  define BL_UP_PIN     _PA_21
#  define BL_DOWN_PIN   _PA_27
#endif

static gpio_irq_t gpio_bl_up;
static gpio_irq_t gpio_bl_down;

static volatile bool     g_pending_bl_up   = false;
static volatile bool     g_pending_bl_down = false;
static volatile uint32_t g_bl_up_press_ms  = 0;
static volatile uint32_t g_bl_down_press_ms = 0;

/* ========================================================================
 * OSD state
 * ======================================================================== */
static lv_obj_t *g_osd = NULL;
static lv_obj_t *g_osd_bar = NULL;
static lv_obj_t *g_osd_label = NULL;

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

/* ========================================================================
 * Brightness up ISR (falling edge = press)
 * ======================================================================== */
static void bl_up_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;
    g_bl_up_press_ms = isr_safe_tick_ms();
    g_pending_bl_up  = true;
}

/* ========================================================================
 * Brightness down ISR (falling edge = press)
 * ======================================================================== */
static void bl_down_irq_handler(uint32_t id, uint32_t event)
{
    (void)id;
    (void)event;
    g_bl_down_press_ms = isr_safe_tick_ms();
    g_pending_bl_down  = true;
}

/* ========================================================================
 * Brightness OSD — transient overlay at bottom-centre
 * ======================================================================== */

static void osd_fadeout_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    if (g_osd != NULL)
    {
        lv_obj_delete(g_osd);
        g_osd = NULL;
        g_osd_bar = NULL;
        g_osd_label = NULL;
    }
}

static void osd_fadein_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void brightness_osd_show(int percent)
{
    /* Delete any existing OSD first */
    if (g_osd != NULL)
    {
        lv_anim_delete(g_osd, NULL);
        lv_obj_delete(g_osd);
        g_osd = NULL;
    }

    lv_obj_t *scr = lv_scr_act();

    /* ---- Container: rounded panel at bottom centre ---- */
    g_osd = lv_obj_create(scr);
    lv_obj_remove_flag(g_osd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_osd, 180, 44);
    lv_obj_align(g_osd, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(g_osd, 22, 0);
    lv_obj_set_style_bg_color(g_osd, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(g_osd, LV_OPA_60, 0);
    lv_obj_set_style_border_width(g_osd, 0, 0);
    lv_obj_set_style_pad_all(g_osd, 0, 0);

    /* ---- Icon + percent label ---- */
    g_osd_label = lv_label_create(g_osd);
    lv_label_set_text_fmt(g_osd_label, "%d %%", (int)percent);
    lv_obj_set_style_text_color(g_osd_label, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_font(g_osd_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_osd_label, LV_ALIGN_LEFT_MID, 10, 0);

    /* ---- Progress bar ---- */
    g_osd_bar = lv_bar_create(g_osd);
    lv_obj_set_size(g_osd_bar, 90, 6);
    lv_obj_align(g_osd_bar, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_bar_set_range(g_osd_bar, 0, 100);
    lv_bar_set_value(g_osd_bar, (int)percent, LV_ANIM_OFF);
    lv_obj_set_style_radius(g_osd_bar, 3, 0);
    lv_obj_set_style_bg_color(g_osd_bar, lv_color_make(0x44, 0x44, 0x44), 0);
    lv_obj_set_style_bg_opa(g_osd_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_anim_duration(g_osd_bar, 200, 0);

    /* LVGL 9.3: bar indicator is styled via LV_PART_INDICATOR on the bar */
    lv_obj_set_style_bg_color(g_osd_bar, lv_color_make(0xFF, 0xCC, 0x33), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_osd_bar, 3, LV_PART_INDICATOR);

    /* ---- Fade in (100 ms) ---- */
    lv_obj_set_style_opa(g_osd, LV_OPA_TRANSP, 0);
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, g_osd);
        lv_anim_set_exec_cb(&a, osd_fadein_cb);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&a, 100);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_ready_cb(&a, NULL);
        lv_anim_start(&a);
    }

    /* ---- Auto-fade out after 1.5 s ---- */
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, g_osd);
        lv_anim_set_exec_cb(&a, osd_fadein_cb);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_ready_cb(&a, osd_fadeout_cb);
        lv_anim_set_delay(&a, 1500);
        lv_anim_start(&a);
    }

    /* Move OSD to foreground so it appears above all widgets */
    lv_obj_move_foreground(g_osd);
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

    /* ---- Brightness up ---- */
    if (g_pending_bl_up)
    {
        uint32_t elapsed = isr_safe_tick_ms() - g_bl_up_press_ms;
        bool held = (GPIO_ReadDataBit(BL_UP_PIN) == 0);   /* active low with pull-up */

        if (held && elapsed >= BRIGHTNESS_LONG_MS)
        {
            /* Long press → 100 % */
            g_pending_bl_up = false;
            backlight_set(100);
            brightness_osd_show(backlight_get());
        }
        else if (!held && elapsed >= GPIO_DEBOUNCE_MS)
        {
            /* Released → step up */
            g_pending_bl_up = false;
            backlight_adjust(BL_STEP_PCT);
            brightness_osd_show(backlight_get());
        }
        /* else: still pressed but < 2 s — wait for next poll */
    }

    /* ---- Brightness down ---- */
    if (g_pending_bl_down)
    {
        uint32_t elapsed = isr_safe_tick_ms() - g_bl_down_press_ms;
        bool held = (GPIO_ReadDataBit(BL_DOWN_PIN) == 0);

        if (held && elapsed >= BRIGHTNESS_LONG_MS)
        {
            /* Long press → min */
            g_pending_bl_down = false;
            backlight_set(BL_MIN_PCT);
            brightness_osd_show(backlight_get());
        }
        else if (!held && elapsed >= GPIO_DEBOUNCE_MS)
        {
            /* Released → step down */
            g_pending_bl_down = false;
            backlight_adjust(-BL_STEP_PCT);
            brightness_osd_show(backlight_get());
        }
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

    /* ---- Brightness +/- buttons ---- */
    gpio_irq_init(&gpio_bl_up,   BL_UP_PIN,   bl_up_irq_handler,   0);
    gpio_irq_pull_ctrl(&gpio_bl_up,   PullUp);
    gpio_irq_set(&gpio_bl_up,   IRQ_FALL, 1);
    gpio_irq_enable(&gpio_bl_up);

    gpio_irq_init(&gpio_bl_down, BL_DOWN_PIN, bl_down_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_bl_down, PullUp);
    gpio_irq_set(&gpio_bl_down, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_bl_down);

    RTK_LOGI(TAG, "GPIO buttons initialized (bl up=0x%02X down=0x%02X)\n",
             (unsigned)BL_UP_PIN, (unsigned)BL_DOWN_PIN);
}
