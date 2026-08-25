#include <stdio.h>
#include <stdlib.h>
#include "ui/pc_dashboard_layout.h"
#include "ui/pc_dashboard_theme.h"
#include "assets/icons/icons.h"
#include "ui/pc_dashboard_ui.h"
#include "log.h"

#ifndef TAG
#define TAG "V3_LAYOUT"
#endif

/* ========================================================================
 * Helpers: set_gradient_bg, create_glow_bar, create_card
 * ======================================================================== */

void set_gradient_bg(lv_obj_t* obj, lv_color_t top, lv_color_t bottom)
{
    lv_obj_set_style_bg_color(obj, top, 0);
    lv_obj_set_style_bg_grad_color(obj, bottom, 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
}

lv_obj_t* create_glow_bar(lv_obj_t* parent, int w, int h, lv_color_t track, lv_color_t indicator)
{
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_ON);
    lv_obj_set_style_anim_duration(bar, 500, 0); /* Value change animation 500ms (was 200ms, longer to reduce tearing) */
    lv_obj_set_style_bg_color(bar, track, 0);
    lv_obj_set_style_bg_color(bar, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(bar, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_40, LV_PART_INDICATOR);
    return bar;
}

lv_obj_t* create_card(lv_obj_t* parent, int w, int h, lv_color_t accent, int y_pos)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, 0, y_pos);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 6, 0);
    lv_obj_set_style_shadow_color(card, accent, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    set_gradient_bg(card,
                    lv_color_make(0x15, 0x15, 0x2A),
                    lv_color_make(0x0A, 0x0A, 0x1A));
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* ========================================================================
 * Static helpers
 * ======================================================================== */

/* Linear interpolation between two uint8_t values */
static uint8_t lerp_u8(uint8_t a, uint8_t b, float t)
{
    return (uint8_t) ((float) a + ((float) b - (float) a) * t);
}

/* Forward declarations for CPU circle particle system */
void init_particles(void);
void cpu_particle_timer_cb(lv_timer_t* timer);

/* ========================================================================
 * Layout container tracking
 * ======================================================================== */
static lv_obj_t* g_layout_container              = NULL; /* current visible layout's wrapper */
static lv_obj_t* g_layout_containers[LAYOUT_MAX] = { NULL, NULL, NULL };

/* Flash state for threshold alert blinking (toggled by timer callback) */
bool g_flash_on = false;

void toggle_flash_state(void)
{
    g_flash_on = !g_flash_on;
}

/* Clock dedup — reset on layout destroy to force immediate clock update */
uint32_t v3_last_sec = 0;

/* Threshold alert tracking — set by update_layout_*(), used by fast_flash_tick() */
bool g_cpu_over  = false;
bool g_env_over  = false;
bool g_ram_over  = false;
bool g_disk_over = false;
bool g_bat_over  = false;
bool g_gpu_over  = false;

/* Per-category data-received flags — guard threshold evaluation before JSON data arrives */
bool s_cpu_data_seen  = false;
bool s_env_data_seen  = false;
bool s_ram_data_seen  = false;
bool s_disk_data_seen = false;
bool s_bat_data_seen  = false;
bool s_gpu_data_seen  = false;

/* ========================================================================
 * V3 widget trackers — set by create_layout_*(), used by update_layout_*()
 * ======================================================================== */

/* Layout A — TRIAD */
lv_obj_t *tr_cpu_bar = NULL, *tr_cpu_val = NULL, *tr_cpu_freq = NULL, *tr_cpu_temp = NULL;
lv_obj_t *tr_ram_bar = NULL, *tr_ram_val = NULL, *tr_ram_swap = NULL, *tr_ram_swap2 = NULL;
lv_obj_t *tr_dsk_bar = NULL, *tr_dsk_val = NULL, *tr_dsk_io = NULL;
lv_obj_t *tr_bat_bar = NULL, *tr_bat_val = NULL, *tr_bat_sts = NULL;
lv_obj_t *tr_gpu_bar = NULL, *tr_gpu_val = NULL, *tr_gpu_name = NULL, *tr_gpu_tm = NULL;
lv_obj_t *tr_io_read = NULL, *tr_io_write = NULL;
lv_obj_t *tr_net_tx = NULL, *tr_net_rx = NULL;
lv_obj_t *tr_sys_p = NULL, *tr_sys_c = NULL, *tr_sys_b = NULL, *tr_sys_h = NULL, *tr_sys_o = NULL;
lv_obj_t *tr_env_t = NULL, *tr_env_h = NULL;
lv_obj_t* tr_weather_info = NULL; /* outdoor weather text */
lv_obj_t* tr_weather_icon = NULL; /* outdoor weather icon */
lv_obj_t* tr_weather_main = NULL; /* outdoor weather main group name (e.g. "Clear") */
lv_obj_t* tr_weather_city = NULL; /* "Beijing" */
lv_obj_t *tr_time = NULL, *tr_user = NULL, *tr_bat_icon = NULL;
lv_obj_t *tr_warn_lbl = NULL, *tr_warn_icon = NULL;

/* Layout B — VORTEX */
lv_obj_t *vo_cpu_freq = NULL, *vo_cpu_temp = NULL;
lv_obj_t *vo_ram_bar = NULL, *vo_ram_val = NULL, *vo_ram_swap = NULL, *vo_ram_swap2 = NULL;
lv_obj_t *vo_dsk_bar = NULL, *vo_dsk_val = NULL, *vo_dsk_io = NULL;
lv_obj_t *vo_bat_bar = NULL, *vo_bat_val = NULL, *vo_bat_sts = NULL, *vo_bat_icon = NULL;
lv_obj_t *vo_gpu_bar = NULL, *vo_gpu_val = NULL, *vo_gpu_name = NULL, *vo_gpu_tm = NULL;
lv_obj_t *vo_net_tx = NULL, *vo_net_rx = NULL;
lv_obj_t *vo_sys_p = NULL, *vo_sys_c = NULL, *vo_sys_b = NULL, *vo_sys_h = NULL, *vo_sys_o = NULL;
lv_obj_t *vo_env_t = NULL, *vo_env_h = NULL;
lv_obj_t* vo_weather_info = NULL;
lv_obj_t* vo_weather_icon = NULL;
lv_obj_t* vo_weather_main = NULL;
lv_obj_t* vo_weather_city = NULL;
lv_obj_t *vo_time = NULL, *vo_user = NULL;
lv_obj_t *vo_warn_lbl = NULL, *vo_warn_icon = NULL;
lv_obj_t* vo_cpu_canvas = NULL; /* CPU ring canvas (inside the ring frame) */
/* Canvas buffer for CPU ring — allocated via lv_draw_buf_create() at runtime */
lv_draw_buf_t* s_cpu_canvas_draw_buf = NULL;
/* Baseline canvas frame (bit-exact gradient fill + edge seal), regenerated on layout create */
lv_color32_t* s_canvas_baseline = NULL;

/* CPU circle particle system */
particle_t  s_particles[PARTICLE_COUNT];
float       s_current_pct    = 0.0f;
float       s_target_pct     = 0.0f;
int32_t     s_anim_phase     = 0;
lv_timer_t* s_particle_timer = NULL;

/* CPU circle precomputed boundary cache (avoids ~600 lv_sqrt32() calls per frame) */
int16_t s_circle_half[CIRCLE_H]; /* Outer ring R=68 half-width per y row */
bool    s_circle_cached = false; /* Whether cache has been initialized */

/* Layout C — PULSE */
lv_obj_t *pu_cpu_val = NULL, *pu_cpu_sub = NULL, *pu_cpu_temp = NULL;
lv_obj_t *pu_ram_val = NULL, *pu_ram_sub = NULL, *pu_ram_swap2 = NULL;
lv_obj_t *pu_dsk_val = NULL, *pu_dsk_sub = NULL;
lv_obj_t *pu_bat_val = NULL, *pu_bat_sub = NULL;
lv_obj_t *pu_gpu_val = NULL, *pu_gpu_sub = NULL;
lv_obj_t* pu_net_sub = NULL;
lv_obj_t *pu_sys_p = NULL, *pu_sys_c = NULL, *pu_sys_b = NULL, *pu_sys_o = NULL;
lv_obj_t *pu_env_t = NULL, *pu_env_h = NULL;
lv_obj_t* pu_weather_info = NULL;
lv_obj_t* pu_weather_icon = NULL;
lv_obj_t* pu_weather_main = NULL;
lv_obj_t* pu_weather_city = NULL;
lv_obj_t *pu_time = NULL, *pu_user = NULL;
lv_obj_t *pu_warn_lbl = NULL, *pu_warn_icon = NULL;
/* ========================================================================
 * JSON diff tracking — last displayed values, avoid redundant UI updates
 * Shared across all 3 layouts. Init to sentinel values so first update
 * always triggers.
 * ======================================================================== */

/* CPU */
int   s_last_cpu_pct  = DIFF_INIT_INT;
float s_last_cpu_freq = DIFF_INIT_FLT;
float s_last_cpu_temp = DIFF_INIT_FLT;

/* RAM */
int      s_last_ram_pct   = DIFF_INIT_INT;
uint64_t s_last_mem_used  = 0;
uint64_t s_last_mem_total = 0;
float    s_last_swap_pct  = DIFF_INIT_FLT;

/* DISK */
int      s_last_dsk_pct  = DIFF_INIT_INT;
float    s_last_dsk_io   = DIFF_INIT_FLT;
uint64_t s_last_io_read  = 0;
uint64_t s_last_io_write = 0;

/* BATT */
int s_last_bat_pct     = DIFF_INIT_INT;
int s_last_bat_plugged = -1; /* -1 = uninitialized, ensures first trigger */

/* GPU */
float s_last_gpu_usage    = DIFF_INIT_FLT;
float s_last_gpu_temp     = DIFF_INIT_FLT;
float s_last_gpu_mem      = DIFF_INIT_FLT;
char  s_last_gpu_name[64] = "";

/* NET */
float s_last_net_tx = DIFF_INIT_FLT;
float s_last_net_rx = DIFF_INIT_FLT;

/* SYS */
uint32_t s_last_proc_cnt        = 0;
uint8_t  s_last_cores           = 0;
uint32_t s_last_boot_time       = 0;
char     s_last_hostname[64]    = "";
char     s_last_os_platform[64] = "";

/* ENV */
float s_last_env_temp = DIFF_INIT_FLT;
float s_last_env_humi = DIFF_INIT_FLT;

/* Weather diff tracking */
char  s_last_weather_city[WEATHER_CITY_MAX_LEN] = "";
char  s_last_weather_main[WEATHER_DESC_MAX_LEN] = "";
float s_last_weather_temp                       = DIFF_INIT_FLT;
int   s_last_weather_humi                       = DIFF_INIT_INT;

/* USER */
char s_last_user[32] = "";

/* Top-level stats diff tracking */
bool s_first = true;

/* Flag: reset fast_flash_tick() prev_*_over tracking on next call (set on layout switch) */
bool g_reset_flash_prev = false;
/* ========================================================================
 * Layout switch helpers — reset state for clean layout/theme transition
 * ======================================================================== */

void reset_diff_tracking(void)
{
    s_last_cpu_pct         = DIFF_INIT_INT;
    s_last_cpu_freq        = DIFF_INIT_FLT;
    s_last_cpu_temp        = DIFF_INIT_FLT;
    s_last_ram_pct         = DIFF_INIT_INT;
    s_last_mem_used        = 0;
    s_last_mem_total       = 0;
    s_last_swap_pct        = DIFF_INIT_FLT;
    s_last_dsk_pct         = DIFF_INIT_INT;
    s_last_dsk_io          = DIFF_INIT_FLT;
    s_last_io_read         = 0;
    s_last_io_write        = 0;
    s_last_bat_pct         = DIFF_INIT_INT;
    s_last_bat_plugged     = -1; /* Ensure first trigger always fires */
    s_last_gpu_usage       = DIFF_INIT_FLT;
    s_last_gpu_temp        = DIFF_INIT_FLT;
    s_last_gpu_mem         = DIFF_INIT_FLT;
    s_last_gpu_name[0]     = '\0';
    s_last_net_tx          = DIFF_INIT_FLT;
    s_last_net_rx          = DIFF_INIT_FLT;
    s_last_proc_cnt        = 0;
    s_last_cores           = 0;
    s_last_boot_time       = 0;
    s_last_hostname[0]     = '\0';
    s_last_os_platform[0]  = '\0';
    s_last_env_temp        = DIFF_INIT_FLT;
    s_last_env_humi        = DIFF_INIT_FLT;
    s_last_weather_city[0] = '\0';
    s_last_weather_main[0] = '\0';
    s_last_weather_temp    = DIFF_INIT_FLT;
    s_last_weather_humi    = DIFF_INIT_INT;
    s_last_user[0]         = '\0';
    s_first                = true;
}

void notify_layout_switched(void)
{
    reset_diff_tracking();
    g_cpu_over         = false;
    g_env_over         = false;
    g_ram_over         = false;
    g_disk_over        = false;
    g_bat_over         = false;
    g_gpu_over         = false;
    g_flash_on         = false;
    g_reset_flash_prev = true;
    v3_last_sec        = 0;
    s_current_pct      = 0.0f;
    s_target_pct       = 0.0f;
    s_anim_phase       = 0;
    reset_diff_tracking();
    reset_mqtt_status_tracking();

    /* Re-trigger weather UI display so existing data shows on new layout */
    {
        bool has_valid = false;
        taskENTER_CRITICAL();
        has_valid = g_weather.valid;
        taskEXIT_CRITICAL();
        if (has_valid)
            g_weather_updated = true;
    }
}
/* ========================================================================
 * Fast flash tick — called by a 150ms LVGL timer (independent of 1Hz updates)
 * Only toggles card borders when threshold is exceeded.
 * ======================================================================== */
void fast_flash_tick(void)
{
    const theme_t* th      = &g_themes[g_theme_id];
    lv_obj_t*      env_bar = NULL;

    /* Previous flash-state tracking (for restoration on over→not-over) */
    static bool prev_cpu_over = false, prev_ram_over = false;
    static bool prev_dsk_over = false, prev_bat_over = false, prev_gpu_over = false;
    static bool prev_env_over = false;

    /* Reset tracking after layout/theme switch (stale prev values from old layout) */
    if (g_reset_flash_prev)
    {
        prev_cpu_over      = false;
        prev_ram_over      = false;
        prev_dsk_over      = false;
        prev_bat_over      = false;
        prev_gpu_over      = false;
        prev_env_over      = false;
        g_reset_flash_prev = false;
    }

    /* Restore a card to its original gradient background + accent border/shadow.
     * Call when a card transitions from over→not-over. */
#define RESTORE_CARD(card_obj, accent)                                                      \
    do                                                                                      \
    {                                                                                       \
        if ((card_obj))                                                                     \
        {                                                                                   \
            lv_obj_set_style_border_width((card_obj), 1, 0);                                \
            lv_obj_set_style_border_color((card_obj), (accent), 0);                         \
            lv_obj_set_style_shadow_width((card_obj), 6, 0);                                \
            lv_obj_set_style_shadow_color((card_obj), (accent), 0);                         \
            lv_obj_set_style_shadow_opa((card_obj), LV_OPA_30, 0);                          \
            lv_obj_set_style_bg_color((card_obj), lv_color_make(0x15, 0x15, 0x2A), 0);      \
            lv_obj_set_style_bg_grad_color((card_obj), lv_color_make(0x0A, 0x0A, 0x1A), 0); \
            lv_obj_set_style_bg_grad_dir((card_obj), LV_GRAD_DIR_VER, 0);                   \
            lv_obj_set_style_bg_opa((card_obj), LV_OPA_COVER, 0);                           \
        }                                                                                   \
    } while (0)

    /* Flash ON: toggle warn-color gradient. Flash OFF: transparent bg.
     * Non-over cards are NEVER touched (no else branch).
     * fast_on declared here so macros below can reference it. */
    static bool fast_on = false;
#define FLASH_CARD(card_obj, accent, over_flag)                                          \
    do                                                                                   \
    {                                                                                    \
        if ((card_obj) && (over_flag))                                                   \
        {                                                                                \
            lv_obj_set_style_border_width((card_obj), 2, 0);                             \
            lv_obj_set_style_border_color((card_obj), fast_on ? th->warn : (accent), 0); \
            lv_obj_set_style_shadow_width((card_obj), 8, 0);                             \
            lv_obj_set_style_shadow_color((card_obj), fast_on ? th->warn : (accent), 0); \
            lv_obj_set_style_shadow_opa((card_obj), LV_OPA_40, 0);                       \
            if (fast_on)                                                                 \
            {                                                                            \
                lv_obj_set_style_bg_color((card_obj), th->warn, 0);                      \
                lv_obj_set_style_bg_grad_color((card_obj), lv_color_make(0, 0, 0), 0);   \
                lv_obj_set_style_bg_grad_dir((card_obj), LV_GRAD_DIR_VER, 0);            \
                lv_obj_set_style_bg_main_stop((card_obj), 0, 0);                         \
                lv_obj_set_style_bg_grad_stop((card_obj), 200, 0);                       \
                lv_obj_set_style_bg_opa((card_obj), LV_OPA_40, 0);                       \
            }                                                                            \
            else                                                                         \
            {                                                                            \
                lv_obj_set_style_bg_opa((card_obj), LV_OPA_TRANSP, 0);                   \
                lv_obj_set_style_bg_grad_dir((card_obj), LV_GRAD_DIR_NONE, 0);           \
            }                                                                            \
        }                                                                                \
    } while (0)

#define FLASH_BAR(bar_obj, accent, over_flag)                          \
    do                                                                 \
    {                                                                  \
        if ((bar_obj) && (over_flag))                                  \
        {                                                              \
            lv_obj_set_style_bg_color((bar_obj),                       \
                                      (fast_on) ? th->warn : (accent), \
                                      LV_PART_INDICATOR);              \
        }                                                              \
    } while (0)

    /* Restore a progress bar indicator to its accent color after flash ends */
#define RESTORE_BAR(bar_obj, accent)                                           \
    do                                                                         \
    {                                                                          \
        if ((bar_obj))                                                         \
        {                                                                      \
            lv_obj_set_style_bg_color((bar_obj), (accent), LV_PART_INDICATOR); \
        }                                                                      \
    } while (0)

    /* ---- Restore cards that just exited flash (over→not-over) ---- */
    switch (g_layout_id)
    {
        case LAYOUT_TRIAD:
            if (prev_cpu_over && !g_cpu_over)
            {
                RESTORE_CARD(tr_cpu_val ? lv_obj_get_parent(tr_cpu_val) : NULL, th->cpu);
                RESTORE_BAR(tr_cpu_bar, th->cpu);
            }
            if (prev_ram_over && !g_ram_over)
            {
                RESTORE_CARD(tr_ram_val ? lv_obj_get_parent(tr_ram_val) : NULL, th->ram);
                RESTORE_BAR(tr_ram_bar, th->ram);
            }
            if (prev_dsk_over && !g_disk_over)
            {
                RESTORE_CARD(tr_dsk_val ? lv_obj_get_parent(tr_dsk_val) : NULL, th->disk);
                RESTORE_BAR(tr_dsk_bar, th->disk);
            }
            if (prev_bat_over && !g_bat_over)
            {
                RESTORE_CARD(tr_bat_val ? lv_obj_get_parent(tr_bat_val) : NULL, th->batt);
                RESTORE_BAR(tr_bat_bar, th->batt);
            }
            if (prev_gpu_over && !g_gpu_over)
            {
                RESTORE_CARD(tr_gpu_val ? lv_obj_get_parent(tr_gpu_val) : NULL, th->gpu);
                RESTORE_BAR(tr_gpu_bar, th->gpu);
            }
            break;
        case LAYOUT_VORTEX:
            /* CPU ring frame not used — border drawn directly in canvas edge seal */
            if (prev_ram_over && !g_ram_over)
            {
                RESTORE_CARD(vo_ram_val ? lv_obj_get_parent(vo_ram_val) : NULL, th->ram);
                RESTORE_BAR(vo_ram_bar, th->ram);
            }
            if (prev_dsk_over && !g_disk_over)
            {
                RESTORE_CARD(vo_dsk_val ? lv_obj_get_parent(vo_dsk_val) : NULL, th->disk);
                RESTORE_BAR(vo_dsk_bar, th->disk);
            }
            if (prev_bat_over && !g_bat_over)
            {
                RESTORE_CARD(vo_bat_val ? lv_obj_get_parent(vo_bat_val) : NULL, th->batt);
                RESTORE_BAR(vo_bat_bar, th->batt);
            }
            if (prev_gpu_over && !g_gpu_over)
            {
                RESTORE_CARD(vo_gpu_val ? lv_obj_get_parent(vo_gpu_val) : NULL, th->gpu);
                RESTORE_BAR(vo_gpu_bar, th->gpu);
            }
            break;
        case LAYOUT_PULSE:
            if (prev_cpu_over && !g_cpu_over)
                RESTORE_CARD(pu_cpu_val ? lv_obj_get_parent(pu_cpu_val) : NULL, th->cpu);
            if (prev_ram_over && !g_ram_over)
                RESTORE_CARD(pu_ram_val ? lv_obj_get_parent(pu_ram_val) : NULL, th->ram);
            if (prev_dsk_over && !g_disk_over)
                RESTORE_CARD(pu_dsk_val ? lv_obj_get_parent(pu_dsk_val) : NULL, th->disk);
            if (prev_bat_over && !g_bat_over)
                RESTORE_CARD(pu_bat_val ? lv_obj_get_parent(pu_bat_val) : NULL, th->batt);
            if (prev_gpu_over && !g_gpu_over)
                RESTORE_CARD(pu_gpu_val ? lv_obj_get_parent(pu_gpu_val) : NULL, th->gpu);
            break;
        default:
            break;
    }

    /* Update previous-state tracking */
    prev_cpu_over = g_cpu_over;
    prev_ram_over = g_ram_over;
    prev_dsk_over = g_disk_over;
    /* Env bar restore */
    if (prev_env_over && !g_env_over)
    {
        lv_obj_t* eb = NULL;
        switch (g_layout_id)
        {
            case LAYOUT_TRIAD:
                if (tr_env_t)
                    eb = lv_obj_get_parent(tr_env_t);
                break;
            case LAYOUT_VORTEX:
                if (vo_env_t)
                    eb = lv_obj_get_parent(vo_env_t);
                break;
            case LAYOUT_PULSE:
                if (pu_env_t)
                    eb = lv_obj_get_parent(pu_env_t);
                break;
            default:
                break;
        }
        if (eb)
        {
            lv_obj_set_style_border_width(eb, 1, 0);
            lv_obj_set_style_border_color(eb, lv_color_make(0x11, 0x44, 0x33), 0);
            lv_obj_set_style_shadow_width(eb, 6, 0);
            lv_obj_set_style_shadow_color(eb, lv_color_make(0x00, 0x20, 0x10), 0);
            lv_obj_set_style_shadow_opa(eb, LV_OPA_40, 0);
            lv_obj_set_style_bg_opa(eb, LV_OPA_0, 0);
        }
    }
    prev_env_over = g_env_over;

    prev_bat_over = g_bat_over;
    prev_gpu_over = g_gpu_over;

    /* Early return: no cards currently over threshold */
    if (!g_cpu_over && !g_env_over && !g_ram_over && !g_disk_over && !g_bat_over && !g_gpu_over)
        return;

    fast_on = !fast_on;

    /* ---- Apply flash to over-threshold cards ---- */
    switch (g_layout_id)
    {
        case LAYOUT_TRIAD:
            FLASH_CARD(tr_cpu_val ? lv_obj_get_parent(tr_cpu_val) : NULL, th->cpu, g_cpu_over);
            FLASH_CARD(tr_ram_val ? lv_obj_get_parent(tr_ram_val) : NULL, th->ram, g_ram_over);
            FLASH_CARD(tr_dsk_val ? lv_obj_get_parent(tr_dsk_val) : NULL, th->disk, g_disk_over);
            FLASH_CARD(tr_bat_val ? lv_obj_get_parent(tr_bat_val) : NULL, th->batt, g_bat_over);
            FLASH_CARD(tr_gpu_val ? lv_obj_get_parent(tr_gpu_val) : NULL, th->gpu, g_gpu_over);
            FLASH_BAR(tr_cpu_bar, th->cpu, g_cpu_over);
            FLASH_BAR(tr_ram_bar, th->ram, g_ram_over);
            FLASH_BAR(tr_dsk_bar, th->disk, g_disk_over);
            FLASH_BAR(tr_bat_bar, th->batt, g_bat_over);
            FLASH_BAR(tr_gpu_bar, th->gpu, g_gpu_over);
            if (tr_env_t)
                env_bar = lv_obj_get_parent(tr_env_t);
            break;

        case LAYOUT_VORTEX:
            /* CPU ring_frame: keep normal style, canvas flash arc removed.
             * Overload is reflected only through heat_color gradient on water ripples,
             * no additional flash animation to avoid LVGL invalidation/mirror artifacts. */
            FLASH_CARD(vo_ram_val ? lv_obj_get_parent(vo_ram_val) : NULL, th->ram, g_ram_over);
            FLASH_CARD(vo_dsk_val ? lv_obj_get_parent(vo_dsk_val) : NULL, th->disk, g_disk_over);
            FLASH_CARD(vo_bat_val ? lv_obj_get_parent(vo_bat_val) : NULL, th->batt, g_bat_over);
            FLASH_CARD(vo_gpu_val ? lv_obj_get_parent(vo_gpu_val) : NULL, th->gpu, g_gpu_over);
            FLASH_BAR(vo_ram_bar, th->ram, g_ram_over);
            FLASH_BAR(vo_dsk_bar, th->disk, g_disk_over);
            FLASH_BAR(vo_bat_bar, th->batt, g_bat_over);
            FLASH_BAR(vo_gpu_bar, th->gpu, g_gpu_over);
            if (vo_env_t)
                env_bar = lv_obj_get_parent(vo_env_t);
            break;

        case LAYOUT_PULSE:
            FLASH_CARD(pu_cpu_val ? lv_obj_get_parent(pu_cpu_val) : NULL, th->cpu, g_cpu_over);
            FLASH_CARD(pu_ram_val ? lv_obj_get_parent(pu_ram_val) : NULL, th->ram, g_ram_over);
            FLASH_CARD(pu_dsk_val ? lv_obj_get_parent(pu_dsk_val) : NULL, th->disk, g_disk_over);
            FLASH_CARD(pu_bat_val ? lv_obj_get_parent(pu_bat_val) : NULL, th->batt, g_bat_over);
            FLASH_CARD(pu_gpu_val ? lv_obj_get_parent(pu_gpu_val) : NULL, th->gpu, g_gpu_over);
            /* Pulse has no progress bars */
            if (pu_env_t)
                env_bar = lv_obj_get_parent(pu_env_t);
            break;

        default:
            break;
    }

    /* ---- Env bar flash (temperature only) — only touches when over threshold ---- */
    if (env_bar && g_env_over)
    {
        if (fast_on)
        {
            lv_obj_set_style_border_width(env_bar, 3, 0);
            lv_obj_set_style_border_color(env_bar, th->warn, 0);
            lv_obj_set_style_shadow_width(env_bar, 20, 0);
            lv_obj_set_style_shadow_color(env_bar, th->warn, 0);
            lv_obj_set_style_shadow_opa(env_bar, LV_OPA_70, 0);
            lv_obj_set_style_bg_color(env_bar, th->warn, 0);
            lv_obj_set_style_bg_opa(env_bar, LV_OPA_90, 0);
        }
        else
        {
            lv_obj_set_style_border_width(env_bar, 1, 0);
            lv_obj_set_style_border_color(env_bar, lv_color_make(0x11, 0x44, 0x33), 0);
            lv_obj_set_style_shadow_width(env_bar, 6, 0);
            lv_obj_set_style_shadow_color(env_bar, lv_color_make(0x00, 0x20, 0x10), 0);
            lv_obj_set_style_shadow_opa(env_bar, LV_OPA_40, 0);
            lv_obj_set_style_bg_opa(env_bar, LV_OPA_0, 0);
        }
    }
}

void set_layout_container(lv_obj_t* cont)
{
    g_layout_container = cont;
    if (g_layout_id < LAYOUT_MAX)
        g_layout_containers[g_layout_id] = cont;
}

lv_obj_t* layout_get_container(void)
{
    return g_layout_container;
}

/* ========================================================================
 * Internal: NULL widget pointers for a specific layout id
 * ======================================================================== */
static void null_widget_pointers_for_id(layout_id_t id)
{
    switch (id)
    {
            case LAYOUT_TRIAD:
                tr_cpu_bar      = NULL;
                tr_cpu_val      = NULL;
                tr_cpu_freq     = NULL;
                tr_cpu_temp     = NULL;
                tr_ram_bar      = NULL;
                tr_ram_val      = NULL;
                tr_ram_swap     = NULL;
                tr_ram_swap2    = NULL;
                tr_dsk_bar      = NULL;
                tr_dsk_val      = NULL;
                tr_dsk_io       = NULL;
                tr_bat_bar      = NULL;
                tr_bat_val      = NULL;
                tr_bat_sts      = NULL;
                tr_gpu_bar      = NULL;
                tr_gpu_val      = NULL;
                tr_gpu_name     = NULL;
                tr_gpu_tm       = NULL;
                tr_io_read      = NULL;
                tr_io_write     = NULL;
                tr_net_tx       = NULL;
                tr_net_rx       = NULL;
                tr_sys_p        = NULL;
                tr_sys_c        = NULL;
                tr_sys_b        = NULL;
                tr_sys_h        = NULL;
                tr_sys_o        = NULL;
                tr_env_t        = NULL;
                tr_env_h        = NULL;
                tr_weather_info = NULL;
                tr_weather_icon = NULL;
                tr_weather_main = NULL;
                tr_weather_city = NULL;
                tr_time         = NULL;
                tr_user         = NULL;
                tr_bat_icon     = NULL;
                tr_warn_lbl     = NULL;
                tr_warn_icon    = NULL;
                break;
            case LAYOUT_VORTEX:
                vo_cpu_freq     = NULL;
                vo_cpu_temp     = NULL;
                vo_cpu_canvas   = NULL;
                s_cpu_canvas_draw_buf = NULL;
                vo_ram_bar      = NULL;
                vo_ram_val      = NULL;
                vo_ram_swap     = NULL;
                vo_ram_swap2    = NULL;
                vo_dsk_bar      = NULL;
                vo_dsk_val      = NULL;
                vo_dsk_io       = NULL;
                vo_bat_bar      = NULL;
                vo_bat_val      = NULL;
                vo_bat_sts      = NULL;
                vo_bat_icon     = NULL;
                vo_gpu_bar      = NULL;
                vo_gpu_val      = NULL;
                vo_gpu_name     = NULL;
                vo_gpu_tm       = NULL;
                vo_net_tx       = NULL;
                vo_net_rx       = NULL;
                vo_sys_p        = NULL;
                vo_sys_c        = NULL;
                vo_sys_b        = NULL;
                vo_sys_h        = NULL;
                vo_sys_o        = NULL;
                vo_env_t        = NULL;
                vo_env_h        = NULL;
                vo_weather_info = NULL;
                vo_weather_icon = NULL;
                vo_weather_main = NULL;
                vo_weather_city = NULL;
                vo_time         = NULL;
                vo_user         = NULL;
                vo_warn_lbl     = NULL;
                vo_warn_icon    = NULL;
                break;
            case LAYOUT_PULSE:
                pu_cpu_val      = NULL;
                pu_cpu_sub      = NULL;
                pu_cpu_temp     = NULL;
                pu_ram_val      = NULL;
                pu_ram_sub      = NULL;
                pu_ram_swap2    = NULL;
                pu_dsk_val      = NULL;
                pu_dsk_sub      = NULL;
                pu_bat_val      = NULL;
                pu_bat_sub      = NULL;
                pu_gpu_val      = NULL;
                pu_gpu_sub      = NULL;
                pu_net_sub      = NULL;
                pu_sys_p        = NULL;
                pu_sys_c        = NULL;
                pu_sys_b        = NULL;
                pu_sys_o        = NULL;
                pu_env_t        = NULL;
                pu_env_h        = NULL;
                pu_weather_info = NULL;
                pu_weather_icon = NULL;
                pu_weather_main = NULL;
                pu_weather_city = NULL;
                pu_time         = NULL;
                pu_user         = NULL;
                pu_warn_lbl     = NULL;
                pu_warn_icon    = NULL;
                break;
            default:
                break;
    }
}

void destroy_current_layout(void)
{
    if (g_layout_id < LAYOUT_MAX && g_layout_containers[g_layout_id] != NULL)
    {
        RTK_LOGI(TAG, "destroy_current_layout [%d]\n", (int) g_layout_id);

        /* Destroy particle timer BEFORE lv_obj_delete kills the canvas,
         * preventing cpu_particle_timer_cb() from accessing a destroyed canvas. */
        if (g_layout_id == LAYOUT_VORTEX)
        {
            if (s_particle_timer)
            {
                lv_timer_delete(s_particle_timer);
                s_particle_timer = NULL;
            }
            if (s_canvas_baseline)
            {
                lv_free(s_canvas_baseline);
                s_canvas_baseline = NULL;
            }
        }

        /* Save draw_buf pointer BEFORE delete — LVGL 9.3 canvas destructor
         * only calls lv_image_cache_drop() (safe on a live pointer) but
         * does NOT free the draw_buf at all. We must free it manually
         * after lv_obj_delete() to avoid a ~78KB leak per cycle. */
        lv_draw_buf_t* s_canvas_db = s_cpu_canvas_draw_buf;
        s_cpu_canvas_draw_buf      = NULL;

        lv_obj_delete(g_layout_containers[g_layout_id]);
        g_layout_containers[g_layout_id] = NULL;
        g_layout_container               = NULL;

        /* Free canvas draw_buf AFTER lv_obj_delete — LVGL 9.3 canvas
         * destructor only calls lv_image_cache_drop(), NOT
         * lv_draw_buf_destroy(). Without this manual free the 78KB
         * buffer leaks every destroy/create cycle. */
        if (s_canvas_db)
            lv_draw_buf_destroy(s_canvas_db);

        null_widget_pointers_for_id(g_layout_id);
    }
    v3_last_sec = 0;
}

bool layout_is_created(void)
{
    return (g_layout_id < LAYOUT_MAX &&
            g_layout_containers[g_layout_id] != NULL);
}
/* ========================================================================
 * Icon creation helper (image-based icons from img_icons/)
 * LVGL 9.3: use lv_image_create/lv_image_set_recolor (not style-based)
 * ======================================================================== */
lv_obj_t* create_icon_img(lv_obj_t* parent, const lv_img_dsc_t* icon, lv_color_t color, int x, int y)
{
    lv_obj_t* img = lv_img_create(parent);
    lv_img_set_src(img, icon);
    /* A8 alpha mask: recolor gives the theme color, recolor_opa
     * controls how strongly it blends with A8 alpha values.
     * LV_OPA_90 keeps the accent vivid while letting A8 details show. */
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, x, y);
    return img;
}
/* ========================================================================
 * Color interpolation — heat (0–100%)
 * ========================================================================
 * Keyframes:
 *    0% → #00CC44  (green)
 *   50% → #FFCC00  (yellow)
 *   70% → #FFAA00  (orange)
 *   85% → #FF3333  (red)
 *  100% → #CC0000  (deep red)
 * ======================================================================== */
lv_color_t heat_color(float percent)
{
    float   t;
    uint8_t r, g, b;

    if (percent <= 0.0f)
    {
        r = 0x00;
        g = 0xCC;
        b = 0x44;
    }
    else if (percent <= 50.0f)
    {
        /* 0% → 50% : (0x00, 0xCC, 0x44) → (0xFF, 0xCC, 0x00) */
        t = percent / 50.0f;
        r = lerp_u8(0x00, 0xFF, t);
        g = lerp_u8(0xCC, 0xCC, t);
        b = lerp_u8(0x44, 0x00, t);
    }
    else if (percent <= 70.0f)
    {
        /* 50% → 70% : (0xFF, 0xCC, 0x00) → (0xFF, 0xAA, 0x00) */
        t = (percent - 50.0f) / 20.0f;
        r = lerp_u8(0xFF, 0xFF, t);
        g = lerp_u8(0xCC, 0xAA, t);
        b = lerp_u8(0x00, 0x00, t);
    }
    else if (percent <= 85.0f)
    {
        /* 70% → 85% : (0xFF, 0xAA, 0x00) → (0xFF, 0x33, 0x33) */
        t = (percent - 70.0f) / 15.0f;
        r = lerp_u8(0xFF, 0xFF, t);
        g = lerp_u8(0xAA, 0x33, t);
        b = lerp_u8(0x00, 0x33, t);
    }
    else
    {
        /* 85% → 100% : (0xFF, 0x33, 0x33) → (0xCC, 0x00, 0x00) */
        if (percent > 100.0f)
            percent = 100.0f;
        t = (percent - 85.0f) / 15.0f;
        r = lerp_u8(0xFF, 0xCC, t);
        g = lerp_u8(0x33, 0x00, t);
        b = lerp_u8(0x33, 0x00, t);
    }

    return lv_color_make(r, g, b);
}

/* ========================================================================
 * Color interpolation — temperature (°C)
 * ========================================================================
 * Keyframes:
 *   ≤30°C → #00CC44  (green)
 *   60°C  → #FFCC00  (yellow)
 *   ≥80°C → #FF3333  (red)
 * ======================================================================== */
lv_color_t temp_color(float celsius)
{
    float   t;
    uint8_t r, g, b;

    if (celsius <= 30.0f)
    {
        r = 0x00;
        g = 0xCC;
        b = 0x44;
    }
    else if (celsius <= 60.0f)
    {
        /* 30°C → 60°C : (0x00, 0xCC, 0x44) → (0xFF, 0xCC, 0x00) */
        t = (celsius - 30.0f) / 30.0f;
        r = lerp_u8(0x00, 0xFF, t);
        g = lerp_u8(0xCC, 0xCC, t);
        b = lerp_u8(0x44, 0x00, t);
    }
    else if (celsius <= 80.0f)
    {
        /* 60°C → 80°C : (0xFF, 0xCC, 0x00) → (0xFF, 0x33, 0x33) */
        t = (celsius - 60.0f) / 20.0f;
        r = lerp_u8(0xFF, 0xFF, t);
        g = lerp_u8(0xCC, 0x33, t);
        b = lerp_u8(0x00, 0x33, t);
    }
    else
    {
        /* ≥80°C : clamped to red */
        r = 0xFF;
        g = 0x33;
        b = 0x33;
    }

    return lv_color_make(r, g, b);
}
/* ========================================================================
 * Layout constants (matching V2 dimensions)
 * ======================================================================== */

/* Layout A — TRIAD (3-column matrix)
 *
 * Creates the full dashboard on @parent:
 *   Header (800x36)   — branded title, user, clock, NO DATA warning
 *   Main container     — 3 column panels
 *     Left   (276px)   — CPU / RAM / DISK / BATT cards
 *     Middle (246px)   — GPU / DISK I/O cards
 *     Right  (250px)   — NETWORK / SYSTEM cards
 *   Env bar  (784x32)  — temperature & humidity
 *   Footer   (800x22)  — status bar
 */
/* ========================================================================
 * V3 update helpers — clock display
 * ======================================================================== */
void update_clock_v3(lv_obj_t* time_label)
{
    if (g_time_base_ts == 0)
    {
        lv_label_set_text(time_label, "--:--:--");
        return;
    }

    uint32_t now_ms     = rtos_time_get_current_system_time_ms();
    uint32_t elapsed_s  = (now_ms - g_time_base_ms) / 1000;
    uint32_t current_ts = g_time_base_ts + elapsed_s + UTC8_OFFSET_SEC;

    uint16_t y;
    uint8_t  mo, d, h, mi, s;
    unix_to_datetime(current_ts, &y, &mo, &d, &h, &mi, &s);

    /* Validate year; <= 1970 means time base invalid, show "--:--:--"
     * until valid timestamp arrives from later data. */
    if (y <= 1970)
    {
        lv_label_set_text(time_label, "--:--:--");
        return;
    }

    /* Skip if same second to avoid flicker */
    if (current_ts == v3_last_sec)
        return;
    v3_last_sec = current_ts;

    lv_label_set_text_fmt(time_label,
                          "%04d-%02d-%02d %02d:%02d:%02d",
                          (int) y,
                          (int) mo,
                          (int) d,
                          (int) h,
                          (int) mi,
                          (int) s);
}
/* ========================================================================
 * CPU circle particle rendering (Vortex layout)
 * ======================================================================== */

/** Triangle wave: phase 0-255 -> -amplitude ~ +amplitude */
static inline int tri_wave(int phase, int amplitude)
{
    int p = phase & 0xFF;
    if (p < 128)
        return (p * amplitude * 2 / 128) - amplitude;
    else
        return ((255 - p) * amplitude * 2 / 128) - amplitude;
}

/** Random point inside circle */
static bool rand_pt_in_circle(int cx, int cy, int r, int* ox, int* oy)
{
    for (int a = 0; a < 20; a++)
    {
        int x  = 4 + (rand() % (cx * 2 - 8));
        int y  = 4 + (rand() % (cy * 2 - 8));
        int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy < r * r)
        {
            *ox = x;
            *oy = y;
            return true;
        }
    }
    return false;
}

/** Init particle system + precompute circle bounds cache */
void init_particles(void)
{
    int32_t const cx = 70, cy = 70, r = 60;
    for (int i = 0; i < PARTICLE_COUNT; i++)
    {
        int x, y;
        rand_pt_in_circle(cx, cy, r, &x, &y);
        s_particles[i].x_base = (int16_t) x;
        s_particles[i].y_base = (int16_t) y;
        s_particles[i].phase  = (uint8_t) (rand() % 256);
        s_particles[i].speed  = (uint8_t) (1 + (rand() % 3));
        s_particles[i].size   = (uint8_t) (1 + (rand() % 2));
        s_particles[i].alpha  = (uint8_t) (180 + rand() % 76);
    }

    /* Pre-compute circle half-width cache for CPU canvas (cx=70, cy=70, r=68) */
    if (!s_circle_cached)
    {
        for (int y = 0; y < CIRCLE_H; y++)
        {
            int dy = y - CIRCLE_CY;
            if (dy < 0)
                dy = -dy;
            if (dy >= CIRCLE_R)
                s_circle_half[y] = 0;
            else
                s_circle_half[y] = (int16_t) (lv_sqrt32(
                                                  (uint32_t) (CIRCLE_R * CIRCLE_R - dy * dy)) +
                                              1);
        }
        s_circle_cached = true;
    }
}

/** Draw water body fill (uses precomputed circle bounds cache) */
static void draw_water_body(int wl, lv_color32_t* px, int w, int h, int cx, int cy, int r, lv_color_t col)
{
    (void) cx;
    (void) cy;
    (void) r;
    for (int y = wl; y < h; y++)
    {
        int half = s_circle_half[y];
        if (half <= 0)
            continue;
        int x0 = cx - half, x1 = cx + half;
        if (x0 < 0)
            x0 = 0;
        if (x1 >= w)
            x1 = w - 1;
        uint8_t a = (uint8_t) (20 + ((y - wl) * 35 / (h - wl > 0 ? h - wl : 1)));
        if (a > 55)
            a = 55;
        for (int x = x0; x <= x1; x++)
        {
            uint32_t idx = (uint32_t) (y * w + x);
            px[idx]      = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = a };
        }
    }
}

/** Draw water wave line (uses precomputed circle bounds cache) */
static void draw_water_wave(int wl, lv_color32_t* px, int w, int h, int cx, int cy, int r, lv_color_t col, int ph)
{
    (void) cx;
    (void) cy;
    (void) r;
    if (wl >= h || wl < 5)
        return;
    int half = s_circle_half[wl];
    if (half <= 0)
        return;
    int x0 = cx - half, x1 = cx + half;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= w)
        x1 = w - 1;
    for (int x = x0; x <= x1; x++)
    {
        int wo = tri_wave((x * 6 + ph * 2) & 0xFF, 3);
        int wy = wl + wo;
        for (int ly = wy - 1; ly <= wy + 1; ly++)
        {
            if (ly < 0 || ly >= h)
                continue;
            int h2 = s_circle_half[ly];
            if (h2 <= 0)
                continue;
            int xl = x - 2, xr = x + 2;
            if (xl < cx - h2)
                xl = cx - h2;
            if (xr > cx + h2)
                xr = cx + h2;
            for (int lx = xl; lx <= xr; lx++)
            {
                if (lx < 0 || lx >= w)
                    continue;
                px[(uint32_t) (ly * w + lx)] = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = 120 };
            }
        }
    }
}

/** Draw rising bubble particles */
static void draw_particles(lv_color32_t* px, int w, int h, int cx, int cy, int r, int wl, lv_color_t col)
{
    for (int i = 0; i < PARTICLE_COUNT; i++)
    {
        int dx = s_particles[i].x_base - cx, dy = s_particles[i].y_base - cy;
        if (dx * dx + dy * dy >= r * r)
            continue;
        if (s_particles[i].y_base < wl)
            continue;
        if (s_particles[i].x_base < 1 || s_particles[i].x_base >= w - 1 || s_particles[i].y_base < 1 || s_particles[i].y_base >= h - 1)
            continue;
        uint8_t  a   = s_particles[i].alpha;
        uint32_t idx = (uint32_t) (s_particles[i].y_base * w + s_particles[i].x_base);
        px[idx]      = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = a };
        for (int dy2 = -1; dy2 <= 1; dy2++)
        {
            for (int dx2 = -1; dx2 <= 1; dx2++)
            {
                if (dx2 == 0 && dy2 == 0)
                    continue;
                int nx = s_particles[i].x_base + dx2, ny = s_particles[i].y_base + dy2;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                    continue;
                int d2 = nx - cx, d2y = ny - cy;
                if (d2 * d2 + d2y * d2y >= r * r)
                    continue;
                uint32_t nidx = (uint32_t) (ny * w + nx);
                uint8_t  glow = (uint8_t) ((uint32_t) a * 35 / 255);
                /* Unconditional glow draw — bg is rewritten each frame and glow is faint
                 * (alpha 16-35), so it won't wash out the semi-transparent water gradient
                 * (alpha 20-55). Skip checking alpha magnitude, otherwise alpha=255 bg
                 * would prevent glow from drawing entirely. */
                px[nidx] = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = glow };
            }
        }
    }
}

/** Update particle positions (rising bubbles) */
static void update_particles(int h)
{
    int32_t const cx = 70, cy = 70, r = 60;
    for (int i = 0; i < PARTICLE_COUNT; i++)
    {
        s_particles[i].y_base -= (int16_t) (s_particles[i].speed * 5);
        if (s_particles[i].y_base < -5)
        {
            s_particles[i].y_base = (int16_t) (h + (rand() % 20));
            int x, y;
            rand_pt_in_circle(cx, cy, r, &x, &y);
            s_particles[i].x_base = (int16_t) x;
            s_particles[i].alpha  = (uint8_t) (120 + rand() % 100);
        }
    }
}

/** Render CPU circle — restore baseline, draw water/particles/text on top */
static void render_cpu_canvas_particles(int pct)
{
    int32_t const  w = 140, h = 140, cx = 70, cy = 70, radius = 68;

    /* Defensive: guard against detached/destroyed canvas */
    if (!vo_cpu_canvas)
        return;
    lv_draw_buf_t* dbuf = lv_canvas_get_draw_buf(vo_cpu_canvas);
    if (!dbuf || !dbuf->data)
        return;

    int            pctc   = (pct < 0) ? 0 : ((pct > 100) ? 100 : pct);
    lv_color_t     accent = heat_color((float) pctc);
    lv_color32_t*  px     = (lv_color32_t*) dbuf->data;
    int            wl     = (h * (100 - pctc)) / 100;

    /* Vertical gradient precalc (for edge seal + first baseline generation) */
    int32_t const cp_y0 = 16, cp_h = 376;
    lv_color32_t  grad_row[140];
    for (int yi = 0; yi < h; yi++)
    {
        int t              = cp_y0 + yi;
        grad_row[yi].red   = (uint8_t) (18 - 10 * t / cp_h);
        grad_row[yi].green = (uint8_t) (18 - 10 * t / cp_h);
        grad_row[yi].blue  = (uint8_t) (38 - 14 * t / cp_h);
        grad_row[yi].alpha = 0xFF;
    }

    /* First run: generate baseline cache (bit-accurate full-frame gradient) */
    if (s_canvas_baseline == NULL)
    {
        s_canvas_baseline = (lv_color32_t*) lv_malloc(
            (size_t) (w * h) * sizeof(lv_color32_t));
        if (s_canvas_baseline)
        {
            for (int yi = 0; yi < h; yi++)
            {
                lv_color32_t bg   = grad_row[yi];
                uint32_t     base = (uint32_t) (yi * w);
                for (uint32_t i = 0; i < (uint32_t) w; i++)
                    s_canvas_baseline[base + i] = bg;
            }
        }
    }

    /* Restore from baseline (memcpy -> corners+circle edge 100% bit-identical) */
    if (s_canvas_baseline)
        memcpy(px, s_canvas_baseline, (size_t) (w * h) * sizeof(lv_color32_t));
    else
    {
        /* fallback — baseline allocation failed */
        for (int yi = 0; yi < h; yi++)
        {
            lv_color32_t bg   = grad_row[yi];
            uint32_t     base = (uint32_t) (yi * w);
            for (uint32_t i = 0; i < (uint32_t) w; i++)
                px[base + i] = bg;
        }
    }

    /* Water body */
    if (wl < h)
        draw_water_body(wl, px, w, h, cx, cy, radius, accent);
    /* Water wave */
    if (wl < h && wl > 0)
        draw_water_wave(wl, px, w, h, cx, cy, radius, accent, s_anim_phase);
    /* Particles */
    draw_particles(px, w, h, cx, cy, radius, wl, accent);

    /* Edge seal: R=66-68 gradient transition + circle edge (cover wave/particle boundary pixels)
     * d=0: heat_color solid circle edge (1px)
     * d=1: heat_color solid circle edge (2px bold)
     * d=2: gradient seal (cover semi-transparent particle boundary) */
    lv_color32_t accent32 = {
        .red = accent.red, .green = accent.green, .blue = accent.blue, .alpha = 0xFF
    };
    for (int y = 0; y < h; y++)
    {
        int half = s_circle_half[y];
        if (half <= 0)
            continue;
        for (int d = 0; d < 3; d++)
        {
            int hw = half - d;
            if (hw < 0)
                break;
            lv_color32_t col = (d <= 1) ? accent32 : grad_row[y];
            int          xl  = cx - hw;
            if (xl >= 0)
                px[(uint32_t) (y * w + xl)] = col;
            int xr = cx + hw;
            if (xr < w)
                px[(uint32_t) (y * w + xr)] = col;
        }
    }

    /* Percentage text + CPU label (single layer) */
    {
        lv_layer_t layer;
        lv_canvas_init_layer(vo_cpu_canvas, &layer);

        /* Percentage */
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d%%", pctc);
        dsc.text    = buf;
        dsc.font    = &lv_font_montserrat_28;
        dsc.color   = lv_color_make(0xFF, 0xFF, 0xFF);
        dsc.align   = LV_TEXT_ALIGN_CENTER;
        lv_area_t a = { cx - 36, cy - 16, cx + 36, cy + 16 };
        lv_draw_label(&layer, &dsc, &a);

        /* CPU label */
        lv_draw_label_dsc_init(&dsc);
        dsc.text     = "CPU";
        dsc.font     = &lv_font_montserrat_16;
        dsc.color    = accent;
        dsc.align    = LV_TEXT_ALIGN_CENTER;
        lv_area_t a2 = { cx - 18, cy + 24, cx + 18, cy + 42 };
        lv_draw_label(&layer, &dsc, &a2);

        lv_canvas_finish_layer(vo_cpu_canvas, &layer);
    }
    lv_obj_invalidate(vo_cpu_canvas);
}

/** Particle animation timer callback (500ms = 2fps) */
void cpu_particle_timer_cb(lv_timer_t* timer)
{
    (void) timer;
    if (!vo_cpu_canvas)
        return;
    s_current_pct += (s_target_pct - s_current_pct) * 0.34f;
    if (s_current_pct < 0.01f && s_target_pct < 0.01f)
        s_current_pct = 0.0f;
    if (s_current_pct > 99.99f && s_target_pct > 99.99f)
        s_current_pct = 100.0f;
    s_anim_phase = (s_anim_phase + 15) % 256;
    update_particles(140);
    render_cpu_canvas_particles((int) (s_current_pct + 0.5f));
}
/* ========================================================================
 * V3 update dispatch — called by update_dashboard_ui()
 * ======================================================================== */
extern volatile bool g_sht3x_pending;

void update_current_layout(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    /* Note: has_data may be false after pc_stats_reset_to_default() on timeout.
     * We still run the update so reset values are rendered on screen instead
     * of frozen stale data. The per-layout functions handle this gracefully:
     * fields reset to zero show "0%", N/A fields show placeholders.        */

    /* JSON diff: skip all widget updates if display-visible fields haven't
     * changed since last display cycle. */
    static PC_Stats_t s_prev = { 0 };

    bool changed = s_first ||
                   stats.cpu != s_prev.cpu ||
                   stats.mem != s_prev.mem ||
                   stats.disk != s_prev.disk ||
                   stats.net_upload_kbps != s_prev.net_upload_kbps ||
                   stats.net_download_kbps != s_prev.net_download_kbps ||
                   stats.cpu_temp_valid != s_prev.cpu_temp_valid ||
                   stats.cpu_temp != s_prev.cpu_temp ||
                   stats.cpu_freq_current != s_prev.cpu_freq_current ||
                   stats.cpu_freq_max != s_prev.cpu_freq_max ||
                   stats.battery_percent != s_prev.battery_percent ||
                   stats.battery_plugged != s_prev.battery_plugged ||
                   stats.gpu_usage != s_prev.gpu_usage ||
                   stats.gpu_temp_c != s_prev.gpu_temp_c ||
                   stats.gpu_mem_used_mb != s_prev.gpu_mem_used_mb ||
                   stats.mem_used != s_prev.mem_used ||
                   stats.mem_total != s_prev.mem_total ||
                   stats.swap_percent != s_prev.swap_percent ||
                   stats.disk_io_percent != s_prev.disk_io_percent ||
                   stats.disk_read_bytes != s_prev.disk_read_bytes ||
                   stats.disk_write_bytes != s_prev.disk_write_bytes ||
                   stats.process_count != s_prev.process_count ||
                   stats.sht3x_temperature != s_prev.sht3x_temperature ||
                   stats.sht3x_humidity != s_prev.sht3x_humidity ||
                   strcmp(stats.current_user, s_prev.current_user) != 0 ||
                   strcmp(stats.hostname, s_prev.hostname) != 0 ||
                   strcmp(stats.os_platform, s_prev.os_platform) != 0 ||
                   strcmp(stats.gpu_name, s_prev.gpu_name) != 0 ||
                   g_sht3x_pending; /* SHT3X pending env-bar update */

    if (!changed)
        return;

    s_first = false;
    s_prev  = stats;

    /* At least one field changed — dispatch to active layout */
    switch (g_layout_id)
    {
        case LAYOUT_TRIAD:
            update_layout_triad();
            break;
        case LAYOUT_VORTEX:
            update_layout_vortex();
            break;
        case LAYOUT_PULSE:
            update_layout_pulse();
            break;
        default:
            break;
    }
    g_sht3x_pending = false; /* env bar updated by layout refresh */
}
/* ========================================================================
 * V3 clock update — called from dashboard_timer_cb() each second
 * ======================================================================== */
void update_layout_clock(void)
{
    switch (g_layout_id)
    {
        case LAYOUT_TRIAD:
            update_clock_v3(tr_time);
            break;
        case LAYOUT_VORTEX:
            update_clock_v3(vo_time);
            break;
        case LAYOUT_PULSE:
            update_clock_v3(pu_time);
            break;
        default:
            break;
    }
}
/* ========================================================================
 * Weather icon lookup — maps OpenWeatherMap main group to the matching
 * 32×32 A8 icon. Falls back to sun icon for unknown conditions.
 * ======================================================================== */
static const lv_image_dsc_t* get_weather_icon(const char* main)
{
    if (main == NULL || main[0] == '\0')
        return &icon_sun;
    if (strcmp(main, "Clear") == 0)
        return &icon_sun;
    if (strcmp(main, "Clouds") == 0)
        return &icon_cloud;
    if (strcmp(main, "Rain") == 0)
        return &icon_rain;
    if (strcmp(main, "Drizzle") == 0)
        return &icon_drizzle;
    if (strcmp(main, "Thunderstorm") == 0)
        return &icon_thunderstorm;
    if (strcmp(main, "Snow") == 0)
        return &icon_snow;
    if (strcmp(main, "Mist") == 0)
        return &icon_fog;
    if (strcmp(main, "Fog") == 0)
        return &icon_fog;
    if (strcmp(main, "Haze") == 0)
        return &icon_fog;
    if (strcmp(main, "Smoke") == 0)
        return &icon_fog;
    if (strcmp(main, "Dust") == 0)
        return &icon_fog;
    if (strcmp(main, "Sand") == 0)
        return &icon_fog;
    if (strcmp(main, "Squall") == 0)
        return &icon_cloud;
    if (strcmp(main, "Tornado") == 0)
        return &icon_cloud;
    return &icon_sun;
}
/* ========================================================================
 * Weather UI update — called from dashboard timer callback
 * Updates weather icon and labels independently of MQTT data flow.
 * ======================================================================== */
void update_weather_ui(void)
{
    /* ====================================================================
     * TEST MODE: cycle through all 7 weather icons every ~5 seconds
     * Uncomment WEATHER_ICON_TEST to enable cycling.
     * ==================================================================== */
    /*  #define WEATHER_ICON_TEST */
#ifdef WEATHER_ICON_TEST
    {
        static unsigned int test_idx     = 0;
        static const char*  test_main[7] = {
            "Clear", "Clouds", "Rain", "Drizzle", "Thunderstorm", "Snow", "Fog"
        };
        static const float test_temp[7] = { 28, 22, 15, 12, 18, -2, 10 };
        static const int   test_humi[7] = { 45, 70, 90, 95, 85, 80, 75 };

        unsigned int idx = test_idx % 7;
        char         info_buf[48];
        int          w_f = (int) (test_temp[idx] * 9.0f / 5.0f + 32.0f + 0.5f);
        snprintf(info_buf, sizeof(info_buf), "%.0f\xC2\xB0\x43 / %d\xC2\xB0\x46  %d%%", (double) test_temp[idx], w_f, test_humi[idx]);

        const lv_image_dsc_t* icon = get_weather_icon(test_main[idx]);

        switch (g_layout_id)
        {
            case LAYOUT_TRIAD:
                if (tr_weather_info)
                    lv_label_set_text(tr_weather_info, info_buf);
                if (tr_weather_main)
                    lv_label_set_text(tr_weather_main, test_main[idx]);
                if (tr_weather_icon)
                    lv_image_set_src(tr_weather_icon, icon);
                break;
            case LAYOUT_VORTEX:
                if (vo_weather_info)
                    lv_label_set_text(vo_weather_info, info_buf);
                if (vo_weather_main)
                    lv_label_set_text(vo_weather_main, test_main[idx]);
                if (vo_weather_icon)
                    lv_image_set_src(vo_weather_icon, icon);
                break;
            case LAYOUT_PULSE:
                if (pu_weather_info)
                    lv_label_set_text(pu_weather_info, info_buf);
                if (pu_weather_main)
                    lv_label_set_text(pu_weather_main, test_main[idx]);
                if (pu_weather_icon)
                    lv_image_set_src(pu_weather_icon, icon);
                break;
            default:
                break;
        }

        test_idx++;
        return; /* skip normal weather path during test */
    }
#endif
    /* =========== END TEST MODE =========== */

    if (!g_weather_updated)
        return;

    Weather_Data_t w;
    taskENTER_CRITICAL();
    memcpy(&w, &g_weather, sizeof(w));
    g_weather_updated = false;
    taskEXIT_CRITICAL();

    if (!w.valid)
        return;

    /* Check if weather actually changed since last display */
    bool city_changed = (strcmp(w.city, s_last_weather_city) != 0);
    bool main_changed = (strcmp(w.main, s_last_weather_main) != 0);
    bool temp_changed = (w.temp_c != s_last_weather_temp);
    bool humi_changed = (w.humidity != s_last_weather_humi);

    if (!city_changed && !main_changed && !temp_changed && !humi_changed)
        return;

    /* Update tracking */
    strncpy(s_last_weather_city, w.city, sizeof(s_last_weather_city) - 1);
    strncpy(s_last_weather_main, w.main, sizeof(s_last_weather_main) - 1);
    s_last_weather_temp = w.temp_c;
    s_last_weather_humi = w.humidity;

    /* Weather info text: "28°C / 82°F  65%" */
    char info_buf[48];
    int  w_f = (int) (w.temp_c * 9.0f / 5.0f + 32.0f + 0.5f);
    snprintf(info_buf, sizeof(info_buf), "%.0f\xC2\xB0\x43 / %d\xC2\xB0\x46  %d%%", (double) w.temp_c, w_f, w.humidity);

    const lv_image_dsc_t* icon = get_weather_icon(w.main);

    switch (g_layout_id)
    {
        case LAYOUT_TRIAD:
            if (tr_weather_info)
                lv_label_set_text(tr_weather_info, info_buf);
            if (tr_weather_main)
            {
                lv_label_set_text(tr_weather_main, w.description);
                if (tr_weather_city)
                    lv_label_set_text(tr_weather_city, w.city);
            }
            if (tr_weather_icon)
                lv_image_set_src(tr_weather_icon, icon);
            break;
        case LAYOUT_VORTEX:
            if (vo_weather_info)
                lv_label_set_text(vo_weather_info, info_buf);
            if (vo_weather_main)
                lv_label_set_text(vo_weather_main, w.description);
            if (vo_weather_city)
                lv_label_set_text(vo_weather_city, w.city);
            if (vo_weather_icon)
                lv_image_set_src(vo_weather_icon, icon);
            break;
        case LAYOUT_PULSE:
            if (pu_weather_info)
                lv_label_set_text(pu_weather_info, info_buf);
            if (pu_weather_main)
                lv_label_set_text(pu_weather_main, w.description);
            if (pu_weather_city)
                lv_label_set_text(pu_weather_city, w.city);
            if (pu_weather_icon)
                lv_image_set_src(pu_weather_icon, icon);
            break;
        default:
            break;
    }
}
