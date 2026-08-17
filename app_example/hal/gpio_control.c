#include "ui/pc_dashboard_theme.h"
#include "ui/pc_dashboard_layout.h"
#include "core/pc_dashboard.h"
#include "hal/backlight_ctrl.h"
#include "gpio_irq_api.h"
#include "log.h"

#ifndef TAG
#define TAG "V3_GPIO"
#endif

/* ===== Debounce / long-press time windows ===== */
#define GPIO_DEBOUNCE_MS   250
#define BRIGHTNESS_LONG_MS 2000 /* hold ≥ 2 s → jump to min / max */

/* ========================================================================
 * ISR-safe timestamp helper
 * ======================================================================== */
static inline uint32_t isr_safe_tick_ms(void)
{
    return rtos_time_get_current_system_time_ms();
}

/* ===== GPIO interrupt objects ===== */
static gpio_irq_t gpio_layout;
static gpio_irq_t gpio_theme;

/* ===== Deferred switch flags ===== */
static volatile bool g_pending_layout_switch = false;
static volatile bool g_pending_theme_switch  = false;

/* ========================================================================
 * Brightness buttons — pin mapping depends on platform
 * ======================================================================== */

/* DBL070: PB_15 = up, PB_17 = down
 * ST7262: PA_21 = up, PA_27 = down */
#ifdef CONFIG_SCREEN_DBL070
#define BL_UP_PIN   _PB_15
#define BL_DOWN_PIN _PB_17
#elif defined(CONFIG_SCREEN_ST7262)
#define BL_UP_PIN   _PA_21
#define BL_DOWN_PIN _PA_27
#endif

static gpio_irq_t gpio_bl_up;
static gpio_irq_t gpio_bl_down;

static volatile bool     g_pending_bl_up    = false;
static volatile bool     g_pending_bl_down  = false;
static volatile uint32_t g_bl_up_press_ms   = 0;
static volatile uint32_t g_bl_down_press_ms = 0;

/* ===== UI buttons suspended flag (standby mode) ===== */
static volatile bool g_ui_buttons_disabled = false;

/* ========================================================================
 * OSD state
 * ======================================================================== */
static lv_obj_t* g_osd       = NULL;
static lv_obj_t* g_osd_bar   = NULL;
static lv_obj_t* g_osd_label = NULL;

/* ========================================================================
 * Brightness OSD — transient overlay at bottom-centre
 * ======================================================================== */

static void osd_fadeout_cb(lv_anim_t* a)
{
    LV_UNUSED(a);
    if (g_osd != NULL)
    {
        lv_obj_delete(g_osd);
        g_osd       = NULL;
        g_osd_bar   = NULL;
        g_osd_label = NULL;
    }
}

static void osd_fadein_cb(void* var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t*) var, (lv_opa_t) v, 0);
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

    lv_obj_t* scr = lv_scr_act();

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
    lv_label_set_text_fmt(g_osd_label, "%d %%", (int) percent);
    lv_obj_set_style_text_color(g_osd_label, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_font(g_osd_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_osd_label, LV_ALIGN_LEFT_MID, 10, 0);

    /* ---- Progress bar ---- */
    g_osd_bar = lv_bar_create(g_osd);
    lv_obj_set_size(g_osd_bar, 90, 6);
    lv_obj_align(g_osd_bar, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_bar_set_range(g_osd_bar, 0, 100);
    lv_bar_set_value(g_osd_bar, (int) percent, LV_ANIM_OFF);
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

/* ========================================================================
 * Unified GPIO button ISR — decodes which pin triggered from the event
 * parameter (see ameba_gpio.c encoding) and dispatches accordingly.
 * ======================================================================== */
static void button_irq_handler(uint32_t id, uint32_t event)
{
    (void) id;

    uint32_t now = isr_safe_tick_ms();

    /* Decode which pin triggered from event parameter */
    uint8_t port = (event >> 21) & 0x3;
    uint8_t pin  = (event >> 16) & 0x1F;

    /* Debounce: per-pin last-tick storage */
    static uint32_t s_last_tick[3][32] = {0};
    if (now - s_last_tick[port][pin] < GPIO_DEBOUNCE_MS)
        return;
    s_last_tick[port][pin] = now;

#ifdef CONFIG_SCREEN_DBL070
    /* DBL070: all 4 buttons on Port B */
    if (port != 1) /* not Port B */
        return;

    switch (pin)
    {
    case 16: /* PB_16 = Layout */
        if (g_ui_buttons_disabled)
            return;
        g_pending_layout_switch = true;
        break;
    case 14: /* PB_14 = Theme */
        if (g_ui_buttons_disabled)
            return;
        g_pending_theme_switch = true;
        break;
    case 15: /* PB_15 = Brightness UP */
        g_bl_up_press_ms = now;
        g_pending_bl_up  = true;
        break;
    case 17: /* PB_17 = Brightness DOWN */
        g_bl_down_press_ms = now;
        g_pending_bl_down  = true;
        break;
    default:
        break;
    }
#elif defined(CONFIG_SCREEN_ST7262)
    /* ST7262: buttons on Port A (PA_21, PA_27, PA_31) and Port B (PB_0) */
    if (port == 0) /* Port A */
    {
        switch (pin)
        {
        case 21: /* PA_21 = Brightness UP */
            g_bl_up_press_ms = now;
            g_pending_bl_up  = true;
            break;
        case 27: /* PA_27 = Brightness DOWN */
            g_bl_down_press_ms = now;
            g_pending_bl_down  = true;
            break;
        case 31: /* PA_31 = Theme */
            if (g_ui_buttons_disabled)
                return;
            g_pending_theme_switch = true;
            break;
        default:
            break;
        }
    }
    else if (port == 1 && pin == 0) /* PB_0 = Layout */
    {
        if (g_ui_buttons_disabled)
            return;
        g_pending_layout_switch = true;
    }
#endif /* CONFIG_SCREEN_* */
}

/* ========================================================================
 * Deferred processing — called from LVGL timer context
 * ======================================================================== */
void gpio_control_process(void)
{
    /* Standby (CLOCK) mode: clear any stale layout/theme pending flags
     * to prevent a press that squeaked in just before g_screen_state was
     * updated from firing on UNLOCK when layout_is_created() becomes true
     * again. Only brightness control is permitted during standby. */
    if (g_screen_state == SCREEN_STATE_CLOCK)
    {
        g_pending_layout_switch = false;
        g_pending_theme_switch  = false;
        goto process_brightness;
    }

    /* ---- Layout switch (single-shot) ---- */
    if (g_pending_layout_switch && layout_is_created())
    {
        g_pending_layout_switch = false;
        layout_id_t next_id     = (g_layout_id + 1) % LAYOUT_MAX;
        RTK_LOGI(TAG, "DEFERRED layout_switch -> %s\n", layout_get_name(next_id));
        layout_switch(next_id);
    }

    /* ---- Theme switch (single-shot) ---- */
    if (g_pending_theme_switch && layout_is_created())
    {
        g_pending_theme_switch = false;
        theme_id_t next_id     = (g_theme_id + 1) % THEME_MAX;
        RTK_LOGI(TAG, "DEFERRED theme_switch -> %s\n", theme_get_name(next_id));
        theme_switch(next_id);
    }

process_brightness:

    /* ---- Brightness up ---- */
    if (g_pending_bl_up)
    {
        uint32_t elapsed = isr_safe_tick_ms() - g_bl_up_press_ms;
        bool     held    = (GPIO_ReadDataBit(BL_UP_PIN) == 0); /* active low with pull-up */

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
        bool     held    = (GPIO_ReadDataBit(BL_DOWN_PIN) == 0);

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

/* ========================================================================
 * Init all GPIO buttons
 * ======================================================================== */
void gpio_control_init(void)
{
    /* ---- Layout / Theme buttons ---- */
#ifdef CONFIG_SCREEN_DBL070
    gpio_irq_init(&gpio_layout, _PB_16, button_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_layout, PullUp);
    gpio_irq_set(&gpio_layout, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_layout);

    gpio_irq_init(&gpio_theme, _PB_14, button_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_theme, PullUp);
    gpio_irq_set(&gpio_theme, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_theme);
#elif defined(CONFIG_SCREEN_ST7262)
    gpio_irq_init(&gpio_layout, _PB_0, button_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_layout, PullDown);
    gpio_irq_set(&gpio_layout, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_layout);

    gpio_irq_init(&gpio_theme, _PA_31, button_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_theme, PullDown);
    gpio_irq_set(&gpio_theme, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_theme);
#endif

    /* ---- Brightness +/- buttons ---- */
    gpio_irq_init(&gpio_bl_up, BL_UP_PIN, button_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_bl_up, PullUp);
    gpio_irq_set(&gpio_bl_up, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_bl_up);

    gpio_irq_init(&gpio_bl_down, BL_DOWN_PIN, button_irq_handler, 0);
    gpio_irq_pull_ctrl(&gpio_bl_down, PullUp);
    gpio_irq_set(&gpio_bl_down, IRQ_FALL, 1);
    gpio_irq_enable(&gpio_bl_down);

    RTK_LOGI(TAG, "GPIO buttons initialized (bl up=0x%02X down=0x%02X)\n", (unsigned) BL_UP_PIN, (unsigned) BL_DOWN_PIN);
}

/* ========================================================================
 * Standby — suspend / resume layout/theme GPIO IRQs
 *
 * Entering CLOCK (standby) mode: hardware-disable the layout and theme
 * switch GPIO interrupts so no ISR fires at all during standby, reducing
 * CPU load to zero for these pins. The g_ui_buttons_disabled flag catches
 * any stale NVIC pending that may fire during the resume re-enable sequence.
 * ======================================================================== */
void gpio_control_suspend_ui_buttons(void)
{
    g_ui_buttons_disabled = true;
    gpio_irq_set(&gpio_layout, IRQ_FALL, 0);
    gpio_irq_set(&gpio_theme, IRQ_FALL, 0);
    g_pending_layout_switch = false;
    g_pending_theme_switch  = false;
}

void gpio_control_resume_ui_buttons(void)
{
    /* Keep disabled while re-enabling — any stale NVIC pending that fires
     * from the gpio_irq_set() re-enable will hit the disabled guard in the
     * ISR and return without setting a pending flag. */
    g_ui_buttons_disabled = true;
    gpio_irq_set(&gpio_layout, IRQ_FALL, 0); /* Ensure disabled first */
    gpio_irq_set(&gpio_theme, IRQ_FALL, 0);
    g_pending_layout_switch = false;
    g_pending_theme_switch  = false;

    gpio_irq_set(&gpio_layout, IRQ_FALL, 1); /* Re-enable */
    gpio_irq_set(&gpio_theme, IRQ_FALL, 1);  /* Stale ISR fires → guard catches */

    /* Clear again in case stale ISR set a flag, then accept real presses */
    g_pending_layout_switch = false;
    g_pending_theme_switch  = false;
    g_ui_buttons_disabled   = false;
}
