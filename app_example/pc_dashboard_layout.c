#include <stdio.h>
#include <stdlib.h>
#include "pc_dashboard_layout.h"
#include "pc_dashboard_theme.h"
#include "threshold_config.h"
#include "img_icons/icons.h"
#include "pc_dashboard_ui.h"
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

lv_obj_t* create_glow_bar(lv_obj_t* parent, int w, int h,
    lv_color_t track, lv_color_t indicator)
{
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_ON);
    lv_obj_set_style_anim_duration(bar, 500, 0);  /* Value change animation 500ms (was 200ms, longer to reduce tearing) */
    lv_obj_set_style_bg_color(bar, track, 0);
    lv_obj_set_style_bg_color(bar, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(bar, indicator, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_40, LV_PART_INDICATOR);
    return bar;
}

lv_obj_t* create_card(lv_obj_t* parent, int w, int h,
    lv_color_t accent, int y_pos)
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
    return (uint8_t)((float)a + ((float)b - (float)a) * t);
}

/* Forward declarations for CPU circle particle system */
static void init_particles(void);
static void cpu_particle_timer_cb(lv_timer_t* timer);

/* ========================================================================
 * Layout container tracking
 * ======================================================================== */
static lv_obj_t* g_layout_container = NULL;      /* current visible layout's wrapper */
static lv_obj_t* g_layout_containers[LAYOUT_MAX] = { NULL, NULL, NULL };

/* Flash state for threshold alert blinking (toggled by timer callback) */
bool g_flash_on = false;

void toggle_flash_state(void)
{
    g_flash_on = !g_flash_on;
}

/* Clock dedup — reset on layout destroy to force immediate clock update */
static uint32_t v3_last_sec = 0;

/* Threshold alert tracking — set by update_layout_*(), used by fast_flash_tick() */
static bool g_cpu_over = false;
static bool g_env_over = false;
static bool g_ram_over = false;
static bool g_disk_over = false;
static bool g_bat_over = false;
static bool g_gpu_over = false;

/* Per-category data-received flags — guard threshold evaluation before JSON data arrives */
static bool s_cpu_data_seen = false;
static bool s_env_data_seen = false;
static bool s_ram_data_seen = false;
static bool s_disk_data_seen = false;
static bool s_bat_data_seen = false;
static bool s_gpu_data_seen = false;

/* ========================================================================
 * V3 widget trackers — set by create_layout_*(), used by update_layout_*()
 * ======================================================================== */

 /* Layout A — TRIAD */
static lv_obj_t* tr_cpu_bar = NULL, * tr_cpu_val = NULL, * tr_cpu_freq = NULL, * tr_cpu_temp = NULL;
static lv_obj_t* tr_ram_bar = NULL, * tr_ram_val = NULL, * tr_ram_swap = NULL, * tr_ram_swap2 = NULL;
static lv_obj_t* tr_dsk_bar = NULL, * tr_dsk_val = NULL, * tr_dsk_io = NULL;
static lv_obj_t* tr_bat_bar = NULL, * tr_bat_val = NULL, * tr_bat_sts = NULL;
static lv_obj_t* tr_gpu_bar = NULL, * tr_gpu_val = NULL, * tr_gpu_name = NULL, * tr_gpu_tm = NULL;
static lv_obj_t* tr_io_read = NULL, * tr_io_write = NULL;
static lv_obj_t* tr_net_tx = NULL, * tr_net_rx = NULL;
static lv_obj_t* tr_sys_p = NULL, * tr_sys_c = NULL, * tr_sys_b = NULL, * tr_sys_h = NULL, * tr_sys_o = NULL;
static lv_obj_t* tr_env_t = NULL, * tr_env_h = NULL;
static lv_obj_t* tr_time = NULL, * tr_user = NULL, * tr_bat_icon = NULL;
static lv_obj_t* tr_warn_lbl = NULL, * tr_warn_icon = NULL;

/* Layout B — VORTEX */
static lv_obj_t* vo_cpu_freq = NULL, * vo_cpu_temp = NULL;
static lv_obj_t* vo_ram_bar = NULL, * vo_ram_val = NULL, * vo_ram_swap = NULL, * vo_ram_swap2 = NULL;
static lv_obj_t* vo_dsk_bar = NULL, * vo_dsk_val = NULL, * vo_dsk_io = NULL;
static lv_obj_t* vo_bat_bar = NULL, * vo_bat_val = NULL, * vo_bat_sts = NULL, * vo_bat_icon = NULL;
static lv_obj_t* vo_gpu_bar = NULL, * vo_gpu_val = NULL, * vo_gpu_name = NULL, * vo_gpu_tm = NULL;
static lv_obj_t* vo_net_tx = NULL, * vo_net_rx = NULL;
static lv_obj_t* vo_sys_p = NULL, * vo_sys_c = NULL, * vo_sys_b = NULL, * vo_sys_h = NULL, * vo_sys_o = NULL;
static lv_obj_t* vo_env_t = NULL, * vo_env_h = NULL;
static lv_obj_t* vo_time = NULL, * vo_user = NULL;
static lv_obj_t* vo_warn_lbl = NULL, * vo_warn_icon = NULL;
static lv_obj_t* vo_cpu_canvas = NULL;          /* CPU ring canvas (inside the ring frame) */
/* Canvas buffer for CPU ring — allocated via lv_draw_buf_create() at runtime */
static lv_draw_buf_t* s_cpu_canvas_draw_buf = NULL;
/* Baseline canvas frame (bit-exact gradient fill + edge seal), regenerated on layout create */
static lv_color32_t* s_canvas_baseline = NULL;

/* CPU circle particle system */
#define PARTICLE_COUNT  48
typedef struct {
    int16_t x_base;
    int16_t y_base;
    uint8_t phase;
    uint8_t speed;
    uint8_t size;
    uint8_t alpha;
} particle_t;
static particle_t s_particles[PARTICLE_COUNT];
static float    s_current_pct = 0.0f;
static float    s_target_pct = 0.0f;
static int32_t  s_anim_phase = 0;
static lv_timer_t* s_particle_timer = NULL;

/* CPU circle precomputed boundary cache (avoids ~600 lv_sqrt32() calls per frame) */
#define CIRCLE_W     140
#define CIRCLE_H     140
#define CIRCLE_CX    70
#define CIRCLE_CY    70
#define CIRCLE_R     68
static int16_t s_circle_half[CIRCLE_H];       /* Outer ring R=68 half-width per y row */
static bool    s_circle_cached = false;       /* Whether cache has been initialized */

/* Layout C — PULSE */
static lv_obj_t* pu_cpu_val = NULL, * pu_cpu_sub = NULL, * pu_cpu_temp = NULL;
static lv_obj_t* pu_ram_val = NULL, * pu_ram_sub = NULL, * pu_ram_swap2 = NULL;
static lv_obj_t* pu_dsk_val = NULL, * pu_dsk_sub = NULL;
static lv_obj_t* pu_bat_val = NULL, * pu_bat_sub = NULL;
static lv_obj_t* pu_gpu_val = NULL, * pu_gpu_sub = NULL;
static lv_obj_t* pu_net_sub = NULL;
static lv_obj_t* pu_sys_p = NULL, * pu_sys_c = NULL, * pu_sys_b = NULL, * pu_sys_o = NULL;
static lv_obj_t* pu_env_t = NULL, * pu_env_h = NULL;
static lv_obj_t* pu_time = NULL, * pu_user = NULL;
static lv_obj_t* pu_warn_lbl = NULL, * pu_warn_icon = NULL;

/* ========================================================================
 * JSON diff tracking — last displayed values, avoid redundant UI updates
 * Shared across all 3 layouts. Init to sentinel values so first update
 * always triggers.
 * ======================================================================== */
#define DIFF_INIT_INT   (-999)
#define DIFF_INIT_FLT   (-9999.0f)

 /* CPU */
static int      s_last_cpu_pct = DIFF_INIT_INT;
static float    s_last_cpu_freq = DIFF_INIT_FLT;
static float    s_last_cpu_temp = DIFF_INIT_FLT;

/* RAM */
static int      s_last_ram_pct = DIFF_INIT_INT;
static uint64_t s_last_mem_used = 0;
static uint64_t s_last_mem_total = 0;
static float    s_last_swap_pct = DIFF_INIT_FLT;

/* DISK */
static int      s_last_dsk_pct = DIFF_INIT_INT;
static float    s_last_dsk_io = DIFF_INIT_FLT;
static uint64_t s_last_io_read = 0;
static uint64_t s_last_io_write = 0;

/* BATT */
static int      s_last_bat_pct = DIFF_INIT_INT;
static int      s_last_bat_plugged = -1;   /* -1 = uninitialized, ensures first trigger */

/* GPU */
static float    s_last_gpu_usage = DIFF_INIT_FLT;
static float    s_last_gpu_temp = DIFF_INIT_FLT;
static float    s_last_gpu_mem = DIFF_INIT_FLT;
static char     s_last_gpu_name[64] = "";

/* NET */
static float    s_last_net_tx = DIFF_INIT_FLT;
static float    s_last_net_rx = DIFF_INIT_FLT;

/* SYS */
static uint32_t s_last_proc_cnt = 0;
static uint8_t  s_last_cores = 0;
static uint32_t s_last_boot_time = 0;
static char     s_last_hostname[64] = "";
static char     s_last_os_platform[64] = "";

/* ENV */
static float    s_last_env_temp = DIFF_INIT_FLT;
static float    s_last_env_humi = DIFF_INIT_FLT;

/* USER */
static char     s_last_user[32] = "";

/* Top-level stats diff tracking */
static bool s_first = true;

/* Flag: reset fast_flash_tick() prev_*_over tracking on next call (set on layout switch) */
static bool g_reset_flash_prev = false;

/* ========================================================================
 * Layout switch helpers — reset state for clean layout/theme transition
 * ======================================================================== */

void reset_diff_tracking(void)
{
    s_last_cpu_pct = DIFF_INIT_INT;
    s_last_cpu_freq = DIFF_INIT_FLT;
    s_last_cpu_temp = DIFF_INIT_FLT;
    s_last_ram_pct = DIFF_INIT_INT;
    s_last_mem_used = 0;
    s_last_mem_total = 0;
    s_last_swap_pct = DIFF_INIT_FLT;
    s_last_dsk_pct = DIFF_INIT_INT;
    s_last_dsk_io = DIFF_INIT_FLT;
    s_last_io_read = 0;
    s_last_io_write = 0;
    s_last_bat_pct = DIFF_INIT_INT;
    s_last_bat_plugged = -1;          /* Ensure first trigger always fires */
    s_last_gpu_usage = DIFF_INIT_FLT;
    s_last_gpu_temp = DIFF_INIT_FLT;
    s_last_gpu_mem = DIFF_INIT_FLT;
    s_last_gpu_name[0] = '\0';
    s_last_net_tx = DIFF_INIT_FLT;
    s_last_net_rx = DIFF_INIT_FLT;
    s_last_proc_cnt = 0;
    s_last_cores = 0;
    s_last_boot_time = 0;
    s_last_hostname[0] = '\0';
    s_last_os_platform[0] = '\0';
    s_last_env_temp = DIFF_INIT_FLT;
    s_last_env_humi = DIFF_INIT_FLT;
    s_last_user[0] = '\0';
    s_first = true;
}

void notify_layout_switched(void)
{
    g_cpu_over = false;
    g_env_over = false;
    g_ram_over = false;
    g_disk_over = false;
    g_bat_over = false;
    g_gpu_over = false;
    g_flash_on = false;
    g_reset_flash_prev = true;
    v3_last_sec = 0;
    s_current_pct = 0.0f;
    s_target_pct = 0.0f;
    s_anim_phase = 0;
    reset_diff_tracking();
    reset_mqtt_status_tracking();
}

/* ========================================================================
 * Fast flash tick — called by a 150ms LVGL timer (independent of 1Hz updates)
 * Only toggles card borders when threshold is exceeded.
 * ======================================================================== */
void fast_flash_tick(void)
{
    const theme_t* th = &g_themes[g_theme_id];
    lv_obj_t* env_bar = NULL;

    /* Previous flash-state tracking (for restoration on over→not-over) */
    static bool prev_cpu_over = false, prev_ram_over = false;
    static bool prev_dsk_over = false, prev_bat_over = false, prev_gpu_over = false;
    static bool prev_env_over = false;

    /* Reset tracking after layout/theme switch (stale prev values from old layout) */
    if (g_reset_flash_prev)
    {
        prev_cpu_over = false; prev_ram_over = false;
        prev_dsk_over = false; prev_bat_over = false; prev_gpu_over = false;
        prev_env_over = false;
        g_reset_flash_prev = false;
    }

    /* Restore a card to its original gradient background + accent border/shadow.
     * Call when a card transitions from over→not-over. */
#define RESTORE_CARD(card_obj, accent) do { \
    if ((card_obj)) { \
        lv_obj_set_style_border_width((card_obj), 1, 0); \
        lv_obj_set_style_border_color((card_obj), (accent), 0); \
        lv_obj_set_style_shadow_width((card_obj), 6, 0); \
        lv_obj_set_style_shadow_color((card_obj), (accent), 0); \
        lv_obj_set_style_shadow_opa((card_obj), LV_OPA_30, 0); \
        lv_obj_set_style_bg_color((card_obj), lv_color_make(0x15, 0x15, 0x2A), 0); \
        lv_obj_set_style_bg_grad_color((card_obj), lv_color_make(0x0A, 0x0A, 0x1A), 0); \
        lv_obj_set_style_bg_grad_dir((card_obj), LV_GRAD_DIR_VER, 0); \
        lv_obj_set_style_bg_opa((card_obj), LV_OPA_COVER, 0); \
    } \
} while(0)

     /* Flash ON: toggle warn-color gradient. Flash OFF: transparent bg.
      * Non-over cards are NEVER touched (no else branch).
      * fast_on declared here so macros below can reference it. */
    static bool fast_on = false;
#define FLASH_CARD(card_obj, accent, over_flag) do { \
    if ((card_obj) && (over_flag)) { \
        lv_obj_set_style_border_width((card_obj), 2, 0); \
        lv_obj_set_style_border_color((card_obj), fast_on ? th->warn : (accent), 0); \
        lv_obj_set_style_shadow_width((card_obj), 8, 0); \
        lv_obj_set_style_shadow_color((card_obj), fast_on ? th->warn : (accent), 0); \
        lv_obj_set_style_shadow_opa((card_obj), LV_OPA_40, 0); \
        if (fast_on) { \
            lv_obj_set_style_bg_color((card_obj), th->warn, 0); \
            lv_obj_set_style_bg_grad_color((card_obj), lv_color_make(0, 0, 0), 0); \
            lv_obj_set_style_bg_grad_dir((card_obj), LV_GRAD_DIR_VER, 0); \
            lv_obj_set_style_bg_main_stop((card_obj), 0, 0); \
            lv_obj_set_style_bg_grad_stop((card_obj), 200, 0); \
            lv_obj_set_style_bg_opa((card_obj), LV_OPA_40, 0); \
        } else { \
            lv_obj_set_style_bg_opa((card_obj), LV_OPA_TRANSP, 0); \
            lv_obj_set_style_bg_grad_dir((card_obj), LV_GRAD_DIR_NONE, 0); \
        } \
    } \
} while(0)

#define FLASH_BAR(bar_obj, accent, over_flag) do { \
    if ((bar_obj) && (over_flag)) { \
        lv_obj_set_style_bg_color((bar_obj), \
            (fast_on) ? th->warn : (accent), LV_PART_INDICATOR); \
    } \
} while(0)

    /* Restore a progress bar indicator to its accent color after flash ends */
#define RESTORE_BAR(bar_obj, accent) do { \
    if ((bar_obj)) { \
        lv_obj_set_style_bg_color((bar_obj), (accent), LV_PART_INDICATOR); \
    } \
} while(0)

    /* ---- Restore cards that just exited flash (over→not-over) ---- */
    switch (g_layout_id)
    {
    case LAYOUT_TRIAD:
        if (prev_cpu_over && !g_cpu_over) { RESTORE_CARD(tr_cpu_val ? lv_obj_get_parent(tr_cpu_val) : NULL, th->cpu); RESTORE_BAR(tr_cpu_bar, th->cpu); }
        if (prev_ram_over && !g_ram_over) { RESTORE_CARD(tr_ram_val ? lv_obj_get_parent(tr_ram_val) : NULL, th->ram); RESTORE_BAR(tr_ram_bar, th->ram); }
        if (prev_dsk_over && !g_disk_over) { RESTORE_CARD(tr_dsk_val ? lv_obj_get_parent(tr_dsk_val) : NULL, th->disk); RESTORE_BAR(tr_dsk_bar, th->disk); }
        if (prev_bat_over && !g_bat_over) { RESTORE_CARD(tr_bat_val ? lv_obj_get_parent(tr_bat_val) : NULL, th->batt); RESTORE_BAR(tr_bat_bar, th->batt); }
        if (prev_gpu_over && !g_gpu_over) { RESTORE_CARD(tr_gpu_val ? lv_obj_get_parent(tr_gpu_val) : NULL, th->gpu); RESTORE_BAR(tr_gpu_bar, th->gpu); }
        break;
    case LAYOUT_VORTEX:
        /* CPU ring frame not used — border drawn directly in canvas edge seal */
        if (prev_ram_over && !g_ram_over) { RESTORE_CARD(vo_ram_val ? lv_obj_get_parent(vo_ram_val) : NULL, th->ram); RESTORE_BAR(vo_ram_bar, th->ram); }
        if (prev_dsk_over && !g_disk_over) { RESTORE_CARD(vo_dsk_val ? lv_obj_get_parent(vo_dsk_val) : NULL, th->disk); RESTORE_BAR(vo_dsk_bar, th->disk); }
        if (prev_bat_over && !g_bat_over) { RESTORE_CARD(vo_bat_val ? lv_obj_get_parent(vo_bat_val) : NULL, th->batt); RESTORE_BAR(vo_bat_bar, th->batt); }
        if (prev_gpu_over && !g_gpu_over) { RESTORE_CARD(vo_gpu_val ? lv_obj_get_parent(vo_gpu_val) : NULL, th->gpu); RESTORE_BAR(vo_gpu_bar, th->gpu); }
        break;
    case LAYOUT_PULSE:
        if (prev_cpu_over && !g_cpu_over) RESTORE_CARD(pu_cpu_val ? lv_obj_get_parent(pu_cpu_val) : NULL, th->cpu);
        if (prev_ram_over && !g_ram_over) RESTORE_CARD(pu_ram_val ? lv_obj_get_parent(pu_ram_val) : NULL, th->ram);
        if (prev_dsk_over && !g_disk_over) RESTORE_CARD(pu_dsk_val ? lv_obj_get_parent(pu_dsk_val) : NULL, th->disk);
        if (prev_bat_over && !g_bat_over) RESTORE_CARD(pu_bat_val ? lv_obj_get_parent(pu_bat_val) : NULL, th->batt);
        if (prev_gpu_over && !g_gpu_over) RESTORE_CARD(pu_gpu_val ? lv_obj_get_parent(pu_gpu_val) : NULL, th->gpu);
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
        case LAYOUT_TRIAD:  if (tr_env_t) eb = lv_obj_get_parent(tr_env_t); break;
        case LAYOUT_VORTEX: if (vo_env_t) eb = lv_obj_get_parent(vo_env_t); break;
        case LAYOUT_PULSE:  if (pu_env_t) eb = lv_obj_get_parent(pu_env_t); break;
        default: break;
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
    if (!g_cpu_over && !g_env_over && !g_ram_over
        && !g_disk_over && !g_bat_over && !g_gpu_over)
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
        if (tr_env_t) env_bar = lv_obj_get_parent(tr_env_t);
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
        if (vo_env_t) env_bar = lv_obj_get_parent(vo_env_t);
        break;

    case LAYOUT_PULSE:
        FLASH_CARD(pu_cpu_val ? lv_obj_get_parent(pu_cpu_val) : NULL, th->cpu, g_cpu_over);
        FLASH_CARD(pu_ram_val ? lv_obj_get_parent(pu_ram_val) : NULL, th->ram, g_ram_over);
        FLASH_CARD(pu_dsk_val ? lv_obj_get_parent(pu_dsk_val) : NULL, th->disk, g_disk_over);
        FLASH_CARD(pu_bat_val ? lv_obj_get_parent(pu_bat_val) : NULL, th->batt, g_bat_over);
        FLASH_CARD(pu_gpu_val ? lv_obj_get_parent(pu_gpu_val) : NULL, th->gpu, g_gpu_over);
        /* Pulse has no progress bars */
        if (pu_env_t) env_bar = lv_obj_get_parent(pu_env_t);
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

void destroy_current_layout(void)
{
    if (g_layout_id < LAYOUT_MAX && g_layout_containers[g_layout_id] != NULL)
    {
        RTK_LOGI(TAG, "destroy_current_layout [%d]\n", (int)g_layout_id);

        /* Destroy particle timer + draw_buf BEFORE lv_obj_delete kills the canvas,
         * preventing cpu_particle_timer_cb() from accessing a destroyed canvas. */
        if (g_layout_id == LAYOUT_VORTEX)
        {
            if (s_particle_timer)
            {
                lv_timer_delete(s_particle_timer);
                s_particle_timer = NULL;
            }
            if (s_cpu_canvas_draw_buf)
            {
                lv_draw_buf_destroy(s_cpu_canvas_draw_buf);
                s_cpu_canvas_draw_buf = NULL;
            }
            if (s_canvas_baseline)
            {
                lv_free(s_canvas_baseline);
                s_canvas_baseline = NULL;
            }
        }

        lv_obj_delete(g_layout_containers[g_layout_id]);
        g_layout_containers[g_layout_id] = NULL;
        g_layout_container = NULL;

        /* NULL all widget pointers for the destroyed layout (dangling pointer prevention) */
        switch (g_layout_id)
        {
        case LAYOUT_TRIAD:
            tr_cpu_bar = NULL; tr_cpu_val = NULL; tr_cpu_freq = NULL; tr_cpu_temp = NULL;
            tr_ram_bar = NULL; tr_ram_val = NULL; tr_ram_swap = NULL; tr_ram_swap2 = NULL;
            tr_dsk_bar = NULL; tr_dsk_val = NULL; tr_dsk_io = NULL;
            tr_bat_bar = NULL; tr_bat_val = NULL; tr_bat_sts = NULL;
            tr_gpu_bar = NULL; tr_gpu_val = NULL; tr_gpu_name = NULL; tr_gpu_tm = NULL;
            tr_io_read = NULL; tr_io_write = NULL;
            tr_net_tx = NULL; tr_net_rx = NULL;
            tr_sys_p = NULL; tr_sys_c = NULL; tr_sys_b = NULL; tr_sys_h = NULL; tr_sys_o = NULL;
            tr_env_t = NULL; tr_env_h = NULL;
            tr_time = NULL; tr_user = NULL; tr_bat_icon = NULL;
            tr_warn_lbl = NULL; tr_warn_icon = NULL;
            break;
        case LAYOUT_VORTEX:
            lv_anim_delete_all();
            vo_cpu_freq = NULL; vo_cpu_temp = NULL;
            vo_cpu_canvas = NULL;
            vo_ram_bar = NULL; vo_ram_val = NULL; vo_ram_swap = NULL; vo_ram_swap2 = NULL;
            vo_dsk_bar = NULL; vo_dsk_val = NULL; vo_dsk_io = NULL;
            vo_bat_bar = NULL; vo_bat_val = NULL; vo_bat_sts = NULL; vo_bat_icon = NULL;
            vo_gpu_bar = NULL; vo_gpu_val = NULL; vo_gpu_name = NULL; vo_gpu_tm = NULL;
            vo_net_tx = NULL; vo_net_rx = NULL;
            vo_sys_p = NULL; vo_sys_c = NULL; vo_sys_b = NULL; vo_sys_h = NULL; vo_sys_o = NULL;
            vo_env_t = NULL; vo_env_h = NULL;
            vo_time = NULL; vo_user = NULL;
            vo_warn_lbl = NULL; vo_warn_icon = NULL;
            break;
        case LAYOUT_PULSE:
            pu_cpu_val = NULL; pu_cpu_sub = NULL; pu_cpu_temp = NULL;
            pu_ram_val = NULL; pu_ram_sub = NULL; pu_ram_swap2 = NULL;
            pu_dsk_val = NULL; pu_dsk_sub = NULL;
            pu_bat_val = NULL; pu_bat_sub = NULL;
            pu_gpu_val = NULL; pu_gpu_sub = NULL;
            pu_net_sub = NULL;
            pu_sys_p = NULL; pu_sys_c = NULL; pu_sys_b = NULL; pu_sys_o = NULL;
            pu_env_t = NULL; pu_env_h = NULL;
            pu_time = NULL; pu_user = NULL;
            pu_warn_lbl = NULL; pu_warn_icon = NULL;
            break;
        default:
            break;
        }
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
lv_obj_t* create_icon_img(lv_obj_t* parent, const lv_img_dsc_t* icon,
    lv_color_t color, int x, int y)
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
    float t;
    uint8_t r, g, b;

    if (percent <= 0.0f)
    {
        r = 0x00; g = 0xCC; b = 0x44;
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
    float t;
    uint8_t r, g, b;

    if (celsius <= 30.0f)
    {
        r = 0x00; g = 0xCC; b = 0x44;
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
        r = 0xFF; g = 0x33; b = 0x33;
    }

    return lv_color_make(r, g, b);
}

/* ========================================================================
 * Layout constants (matching V2 dimensions)
 * ======================================================================== */

 /* Panel / card layout */
#define TRIAD_PANEL_HEIGHT      376
#define TRIAD_CARD_GAP          4
#define TRIAD_CARD_TOP_OFFSET   4
#define TRIAD_CARD_LEFT_CH      89      /* left-panel card height */
#define TRIAD_CARD_MID_GAP      4       /* middle-panel gap */
#define TRIAD_RIGHT_NET_CH      130     /* right-panel NETWORK card height (3x 32px icons need ~130px) */
#define TRIAD_CARD_PADDING_H    6      /* card horizontal padding sum (was 18, reduced for more content width) */
#define TRIAD_CARD_BAR_MARGIN   10      /* bar inset from card edge */
#define TRIAD_CARD_BAR_H        14      /* progress bar height */

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
void create_layout_triad(lv_obj_t* parent)
{
    const theme_t* th = &g_themes[g_theme_id];
    const char* tname = theme_get_name(g_theme_id);
    const char* lname = layout_get_name(LAYOUT_TRIAD);

    /* ----- Wrapper container (for clean destroy) ----- */
    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(wrapper, 0, 0);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_radius(wrapper, 0, 0);
    lv_obj_set_style_bg_opa(wrapper, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_remove_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);
    set_layout_container(wrapper);

    RTK_LOGI(TAG, "create_layout_triad\n");

    /* ==============================================================
     * 1. Header — branded title, user label, clock, NO DATA warning
     * ============================================================== */
    lv_obj_t* header = lv_obj_create(wrapper);
    lv_obj_set_size(header, SCREEN_WIDTH, 36);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_make(0x1A, 0x1A, 0x3A), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_shadow_width(header, 10, 0);
    lv_obj_set_style_shadow_color(header, lv_color_make(0x00, 0x00, 0x30), 0);
    set_gradient_bg(header, th->bg_top, th->bg_bot);

    /* Gear icon (title) */
    create_icon_img(header, &icon_gear, th->header, 12, 2);

    /* Branded title: "PC DASHBOARD · TRIAD · COBALT" */
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf),
        "PC DASHBOARD - %s - %s", lname, tname);
    lv_obj_t* title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title_buf);
    lv_obj_set_style_text_color(title_lbl, th->header, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 52, 0);

    /* User name label (populated by update function) */
    create_icon_img(header, &icon_user,
        lv_color_make(0x88, 0xAA, 0xCC), 430, 2);
    lv_obj_t* user_lbl = lv_label_create(header);
    tr_user = user_lbl;
    lv_label_set_text(user_lbl, "");
    lv_obj_set_style_text_color(user_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(user_lbl, 460, 4);

    /* Clock label */
    lv_obj_t* clock_lbl = lv_label_create(header);
    tr_time = clock_lbl;
    lv_label_set_text(clock_lbl, "--:--:--");
    lv_obj_set_style_text_color(clock_lbl, lv_color_make(0x00, 0xFF, 0x88), 0);
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -2, 0);

    /* NO DATA warning icon + label (hidden by default, toggled by update) */
    lv_obj_t* warn_icon = create_icon_img(header, &icon_warning,
        th->warn, 680, 2);
    tr_warn_icon = warn_icon;
    lv_obj_add_flag(warn_icon, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* warn_lbl = lv_label_create(header);
    tr_warn_lbl = warn_lbl;
    lv_label_set_text(warn_lbl, " NO DATA");
    lv_obj_set_style_text_color(warn_lbl, th->warn, 0);
    lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(warn_lbl, LV_ALIGN_RIGHT_MID, -130, 0);
    lv_obj_add_flag(warn_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ==============================================================
     * 2. Main container (784 x 376) — frames all 3 column panels
     * ============================================================== */
    lv_obj_t* mc = lv_obj_create(wrapper);
    lv_obj_set_size(mc, 792, TRIAD_PANEL_HEIGHT);
    lv_obj_set_pos(mc, 4, 42);
    lv_obj_remove_flag(mc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(mc, 1, 0);
    lv_obj_set_style_border_color(mc, lv_color_make(0x18, 0x18, 0x34), 0);
    lv_obj_set_style_radius(mc, 10, 0);
    lv_obj_set_style_pad_all(mc, 0, 0);
    lv_obj_set_style_shadow_width(mc, 10, 0);
    lv_obj_set_style_shadow_color(mc, lv_color_make(0x00, 0x00, 0x30), 0);
    set_gradient_bg(mc,
        lv_color_make(0x0C, 0x0C, 0x20),
        lv_color_make(0x04, 0x04, 0x12));

    /* ==============================================================
     * 3. Left panel — CPU / RAM / DISK / BATT
     * ============================================================== */
    {
        int pw = 260;
        int cw = pw - TRIAD_CARD_PADDING_H;
        int bw = cw - TRIAD_CARD_BAR_MARGIN;
        int ch = TRIAD_CARD_LEFT_CH;
        int gap = TRIAD_CARD_GAP;
        int ct = TRIAD_CARD_TOP_OFFSET;

        lv_obj_t* lp = lv_obj_create(mc);
        lv_obj_set_size(lp, pw, TRIAD_PANEL_HEIGHT);
        lv_obj_set_pos(lp, 0, 0);
        lv_obj_remove_flag(lp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(lp, 0, 0);
        lv_obj_set_style_radius(lp, 8, 0);
        lv_obj_set_style_pad_all(lp, 0, 0);
        set_gradient_bg(lp,
            lv_color_make(0x12, 0x12, 0x26),
            lv_color_make(0x08, 0x08, 0x18));

        /* ---- CPU card ---- */
        {
            lv_color_t accent = th->cpu;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct);

            create_icon_img(card, &icon_cpu, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "CPU");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_cpu_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            tr_cpu_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* freq = lv_label_create(card);
            tr_cpu_freq = freq;
            lv_label_set_text(freq, "");
            lv_obj_set_style_text_color(freq, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(freq, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(freq, 8, 62);

            lv_obj_t* temp = lv_label_create(card);
            tr_cpu_temp = temp;
            lv_label_set_text(temp, "");
            lv_obj_set_style_text_color(temp, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(temp, 150, 62);
        }

        /* ---- RAM card ---- */
        {
            lv_color_t accent = th->ram;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct + (ch + gap));

            create_icon_img(card, &icon_ram, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "RAM");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_ram_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            tr_ram_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* swap = lv_label_create(card);
            tr_ram_swap = swap;
            lv_label_set_text(swap, "");
            lv_obj_set_style_text_color(swap, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(swap, 8, 62);

            lv_obj_t* swap2 = lv_label_create(card);
            tr_ram_swap2 = swap2;
            lv_label_set_text(swap2, "");
            lv_obj_set_style_text_color(swap2, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap2, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(swap2, 150, 62);
        }

        /* ---- DISK card ---- */
        {
            lv_color_t accent = th->disk;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct + (ch + gap) * 2);

            create_icon_img(card, &icon_disk, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "DISK");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_dsk_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            tr_dsk_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* io = lv_label_create(card);
            tr_dsk_io = io;
            lv_label_set_text(io, "");
            lv_obj_set_style_text_color(io, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(io, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(io, 8, 62);
        }

        /* ---- BATT card ---- */
        {
            lv_color_t accent = th->batt;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct + (ch + gap) * 3);

            create_icon_img(card, &icon_battery, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "BATT");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_bat_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            tr_bat_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            /* Status indicator circle + label */
            lv_obj_t* indicator = lv_obj_create(card);
            lv_obj_set_size(indicator, 8, 8);
            lv_obj_set_pos(indicator, 10, 66);
            lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(indicator, 0, 0);
            lv_obj_set_style_bg_color(indicator, lv_color_make(0x44, 0xDD, 0x44), 0);
            lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
            tr_bat_icon = indicator;
            lv_obj_t* stat = lv_label_create(card);
            tr_bat_sts = stat;
            lv_label_set_text(stat, "");
            lv_obj_set_style_text_color(stat, lv_color_make(0x88, 0xAA, 0x88), 0);
            lv_obj_set_style_text_font(stat, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(stat, 26, 64);
        }
    }

    /* ==============================================================
     * 4. Middle panel — GPU / DISK I/O
     * ============================================================== */
    {
        int pw = 260;
        int cw = pw - TRIAD_CARD_PADDING_H;
        int bw = cw - TRIAD_CARD_BAR_MARGIN;
        int bh = TRIAD_CARD_BAR_H;
        int gap = TRIAD_CARD_MID_GAP;
        int ct = TRIAD_CARD_TOP_OFFSET;
        int ch_gpu = (TRIAD_PANEL_HEIGHT - ct - gap) / 2;
        int ch_io = TRIAD_PANEL_HEIGHT - ct - ch_gpu - gap - 4;

        lv_obj_t* mp = lv_obj_create(mc);
        lv_obj_set_size(mp, pw, TRIAD_PANEL_HEIGHT);
        lv_obj_set_pos(mp, 266, 0);
        lv_obj_remove_flag(mp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(mp, 0, 0);
        lv_obj_set_style_radius(mp, 8, 0);
        lv_obj_set_style_pad_all(mp, 0, 0);
        set_gradient_bg(mp,
            lv_color_make(0x12, 0x12, 0x26),
            lv_color_make(0x08, 0x08, 0x18));

        /* ---- GPU card ---- */
        {
            lv_color_t accent = th->gpu;
            lv_obj_t* card = create_card(mp, cw, ch_gpu, accent, ct);

            create_icon_img(card, &icon_gpu, accent, 8, 6);
            lv_obj_t* gpu_title = lv_label_create(card);
            lv_label_set_text(gpu_title, "GPU");
            lv_obj_set_style_text_color(gpu_title, accent, 0);
            lv_obj_set_style_text_font(gpu_title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(gpu_title, 44, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_gpu_val = val;
            lv_label_set_text(val, "");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, bh,
                lv_color_make(0x22, 0x22, 0x35), accent);
            tr_gpu_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* name = lv_label_create(card);
            tr_gpu_name = name;
            lv_label_set_text(name, "");
            lv_obj_set_style_text_color(name, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(name, 8, 62);

            lv_obj_t* temp_mem = lv_label_create(card);
            tr_gpu_tm = temp_mem;
            lv_label_set_text(temp_mem, "");
            lv_obj_set_style_text_color(temp_mem, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp_mem, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(temp_mem, 8, 84);
        }

        /* ---- DISK I/O card ---- */
        {
            lv_color_t accent = th->io;
            lv_obj_t* card = create_card(mp, cw, ch_io, accent, ct + ch_gpu + gap);

            lv_obj_t* io_title = lv_label_create(card);
            lv_label_set_text(io_title, "DISK I/O");
            lv_obj_set_style_text_color(io_title, accent, 0);
            lv_obj_set_style_text_font(io_title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(io_title, 42, 6);

            /* HDD icon + two-line read/write labels */
            create_icon_img(card, &icon_disk, accent, 8, 2);
            lv_obj_t* read_lbl = lv_label_create(card);
            tr_io_read = read_lbl;
            lv_label_set_text(read_lbl, "");
            lv_obj_set_style_text_color(read_lbl, lv_color_make(0x88, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(read_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(read_lbl, 44, 40);

            lv_obj_t* write_lbl = lv_label_create(card);
            tr_io_write = write_lbl;
            lv_label_set_text(write_lbl, "");
            lv_obj_set_style_text_color(write_lbl, lv_color_make(0x88, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(write_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(write_lbl, 44, 60);

            /* Util label */
            lv_obj_t* util_lbl = lv_label_create(card);
            lv_label_set_text(util_lbl, "");
            lv_obj_set_style_text_color(util_lbl, lv_color_make(0xAA, 0xCC, 0x88), 0);
            lv_obj_set_style_text_font(util_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(util_lbl, 8, 84);
        }
    }

    /* ==============================================================
     * 5. Right panel — NETWORK / SYSTEM
     * ============================================================== */
    {
        int pw = 260;
        int cw = pw - TRIAD_CARD_PADDING_H;
        int gap = TRIAD_CARD_GAP;
        int ct = TRIAD_CARD_TOP_OFFSET;
        int ch_net = TRIAD_RIGHT_NET_CH;
        int ch_sys = TRIAD_PANEL_HEIGHT - ct - ch_net - gap - 4;

        lv_obj_t* rp = lv_obj_create(mc);
        lv_obj_set_size(rp, pw, TRIAD_PANEL_HEIGHT);
        lv_obj_set_pos(rp, 532, 0);
        lv_obj_remove_flag(rp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(rp, 0, 0);
        lv_obj_set_style_radius(rp, 8, 0);
        lv_obj_set_style_pad_all(rp, 0, 0);
        set_gradient_bg(rp,
            lv_color_make(0x12, 0x12, 0x26),
            lv_color_make(0x08, 0x08, 0x18));

        /* ---- NETWORK card ---- */
        {
            lv_color_t accent = th->net;
            lv_obj_t* card = create_card(rp, cw, ch_net, accent, ct);

            create_icon_img(card, &icon_wifi, accent, 8, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "NETWORK");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 42, 16);

            /* TX icon + label */
            create_icon_img(card, &icon_arrow_up,
                lv_color_make(0x88, 0xDD, 0xAA), 10, 38);
            lv_obj_t* up_lbl = lv_label_create(card);
            tr_net_tx = up_lbl;
            lv_label_set_text(up_lbl, "");
            lv_obj_set_style_text_color(up_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(up_lbl, 54, 50);

            /* RX icon + label */
            create_icon_img(card, &icon_arrow_down,
                lv_color_make(0x88, 0xDD, 0xAA), 10, 76);
            lv_obj_t* down_lbl = lv_label_create(card);
            tr_net_rx = down_lbl;
            lv_label_set_text(down_lbl, "");
            lv_obj_set_style_text_color(down_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(down_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(down_lbl, 54, 88);
        }

        /* ---- SYSTEM card ---- */
        {
            lv_color_t accent = th->sys;
            lv_obj_t* card = create_card(rp, cw, ch_sys, accent, ct + ch_net + gap);

            create_icon_img(card, &icon_gear, accent, 8, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "SYSTEM");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 42, 16);

            int lx = 8;
            int ly = 50;
            int lgap = 38;

            /* Processes */
            create_icon_img(card, &icon_list,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11);
            lv_obj_t* proc_lbl = lv_label_create(card);
            tr_sys_p = proc_lbl;
            lv_label_set_text(proc_lbl, "");
            lv_obj_set_style_text_color(proc_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(proc_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(proc_lbl, lx + 40, ly);

            /* Cores */
            create_icon_img(card, &icon_cpu,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap);
            lv_obj_t* cores_lbl = lv_label_create(card);
            tr_sys_c = cores_lbl;
            lv_label_set_text(cores_lbl, "");
            lv_obj_set_style_text_color(cores_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(cores_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(cores_lbl, lx + 40, ly + lgap);

            /* Boot time */
            create_icon_img(card, &icon_power_off,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 2);
            lv_obj_t* boot_lbl = lv_label_create(card);
            tr_sys_b = boot_lbl;
            lv_label_set_text(boot_lbl, "");
            lv_obj_set_style_text_color(boot_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(boot_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(boot_lbl, lx + 40, ly + lgap * 2 - 2);

            /* Hostname */
            create_icon_img(card, &icon_user,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 3);
            lv_obj_t* host_lbl = lv_label_create(card);
            tr_sys_h = host_lbl;
            lv_label_set_text(host_lbl, "");
            lv_obj_set_style_text_color(host_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(host_lbl, lx + 40, ly + lgap * 3 - 2);

            /* OS */
            create_icon_img(card, &icon_globe,
                lv_color_make(0x88, 0xAA, 0xCC), lx, ly - 11 + lgap * 4);
            lv_obj_t* os_lbl = lv_label_create(card);
            tr_sys_o = os_lbl;
            lv_label_set_text(os_lbl, "OS: N/A");
            lv_obj_set_style_text_color(os_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(os_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_width(os_lbl, cw - 26);
            lv_obj_set_pos(os_lbl, lx + 40, ly + lgap * 4 - 2);
        }
    }

    /* ==============================================================
     * 6. Env bar — temperature & humidity
     * ============================================================== */
    {
        int bar_w = 792;
        int bar_h = 32;
        int bx = 4;
        int by = 420;

        lv_obj_t* bar = lv_obj_create(wrapper);
        lv_obj_set_size(bar, bar_w, bar_h);
        lv_obj_set_pos(bar, bx, by);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_make(0x11, 0x44, 0x33), 0);
        lv_obj_set_style_radius(bar, 8, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_shadow_width(bar, 6, 0);
        lv_obj_set_style_shadow_color(bar, lv_color_make(0x00, 0x20, 0x10), 0);
        set_gradient_bg(bar,
            lv_color_make(0x0A, 0x22, 0x16),
            lv_color_make(0x06, 0x14, 0x0C));

        create_icon_img(bar, &icon_temp, th->env, 12, 0);
        lv_obj_t* env_title = lv_label_create(bar);
        lv_label_set_text(env_title, "ENV");
        lv_obj_set_style_text_color(env_title, th->env, 0);
        lv_obj_set_style_text_font(env_title, &lv_font_montserrat_18, 0);
        lv_obj_align(env_title, LV_ALIGN_LEFT_MID, 46, 0);

        lv_obj_t* temp_lbl = lv_label_create(bar);
        tr_env_t = temp_lbl;
        lv_label_set_text(temp_lbl, "Temp: --. \xC2\xB0\x43 / --. \xC2\xB0\x46");
        lv_obj_set_style_text_color(temp_lbl, lv_color_make(0xAA, 0xFF, 0xCC), 0);
        lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 180, 0);

        lv_obj_t* humi_lbl = lv_label_create(bar);
        tr_env_h = humi_lbl;
        lv_label_set_text(humi_lbl, "Humi: --.-%");
        lv_obj_set_style_text_color(humi_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
        lv_obj_set_style_text_font(humi_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(humi_lbl, LV_ALIGN_LEFT_MID, 580, 0);
    }

    /* ==============================================================
     * 7. Footer — status bar
     * ============================================================== */
    {
        lv_obj_t* footer = lv_obj_create(wrapper);
        lv_obj_set_size(footer, SCREEN_WIDTH, 22);
        lv_obj_set_pos(footer, 0, 458);
        lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_style_border_width(footer, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(footer, lv_color_make(0x1A, 0x1A, 0x3A), 0);
        lv_obj_set_style_pad_all(footer, 0, 0);
        lv_obj_set_style_radius(footer, 0, 0);
        set_gradient_bg(footer,
            lv_color_make(0x0E, 0x0E, 0x28),
            lv_color_make(0x06, 0x06, 0x16));

        lv_obj_t* ftr_lbl = lv_label_create(footer);
        lv_label_set_text(ftr_lbl,
            " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
        lv_obj_set_style_text_color(ftr_lbl, lv_color_make(0x66, 0x88, 0xAA), 0);
        lv_obj_set_style_text_font(ftr_lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ftr_lbl, LV_ALIGN_LEFT_MID, 15, 0);
        g_mqtt_status_label = ftr_lbl;   /* Dynamic MQTT status update */
    }
}

/* ========================================================================
 * Layout B — VORTEX (CPU-centered)
 * ========================================================================
 *
 *   Header:         800x36   at (0,0)
 *   Main container: 784x376  at (8,42)
 *     Left   column  172px   — RAM / DISK / BATT cards (3 stack)
 *     Center column  432px   — CPU ring gauge (140x140) + GPU bar below
 *     Right  column  172px   — NETWORK (compact) + SYSTEM cards
 *   Env bar:        784x32   at (8,420)
 *   Footer:         800x22   at (0,458)
 * ======================================================================== */
void create_layout_vortex(lv_obj_t* parent)
{
    const theme_t* th = &g_themes[g_theme_id];
    const char* tname = theme_get_name(g_theme_id);
    const char* lname = layout_get_name(LAYOUT_VORTEX);

    /* ----- Wrapper container (for clean destroy) ----- */
    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(wrapper, 0, 0);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_radius(wrapper, 0, 0);
    lv_obj_set_style_bg_opa(wrapper, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_remove_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);
    set_layout_container(wrapper);

    RTK_LOGI(TAG, "create_layout_vortex\n");

    /* Column widths */
#define VORTEX_LP_W  260   /* left panel */
#define VORTEX_CP_W  260   /* center panel */
#define VORTEX_RP_W  260   /* right panel */

    /* ==============================================================
     * 1. Header — branded title, user, clock, NO DATA warning
     * ============================================================== */
    lv_obj_t* header = lv_obj_create(wrapper);
    lv_obj_set_size(header, SCREEN_WIDTH, 36);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_make(0x1A, 0x1A, 0x3A), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_shadow_width(header, 10, 0);
    lv_obj_set_style_shadow_color(header, lv_color_make(0x00, 0x00, 0x30), 0);
    set_gradient_bg(header, th->bg_top, th->bg_bot);

    /* Gear icon */
    create_icon_img(header, &icon_gear, th->header, 12, 2);

    /* Title */
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf),
        "PC DASHBOARD - %s - %s", lname, tname);
    lv_obj_t* title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title_buf);
    lv_obj_set_style_text_color(title_lbl, th->header, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 52, 0);

    /* User name label */
    create_icon_img(header, &icon_user,
        lv_color_make(0x88, 0xAA, 0xCC), 440, 2);
    lv_obj_t* user_lbl = lv_label_create(header);
    vo_user = user_lbl;
    lv_label_set_text(user_lbl, "");
    lv_obj_set_style_text_color(user_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(user_lbl, 470, 4);

    /* Clock label */
    lv_obj_t* clock_lbl = lv_label_create(header);
    vo_time = clock_lbl;
    lv_label_set_text(clock_lbl, "--:--:--");
    lv_obj_set_style_text_color(clock_lbl, lv_color_make(0x00, 0xFF, 0x88), 0);
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -2, 0);

    /* NO DATA warning */
    lv_obj_t* warn_icon = create_icon_img(header, &icon_warning,
        th->warn, 680, 2);
    vo_warn_icon = warn_icon;
    lv_obj_add_flag(warn_icon, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* warn_lbl = lv_label_create(header);
    lv_label_set_text(warn_lbl, " NO DATA");
    lv_obj_set_style_text_color(warn_lbl, th->warn, 0);
    lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(warn_lbl, LV_ALIGN_RIGHT_MID, -130, 0);
    vo_warn_lbl = warn_lbl;
    lv_obj_add_flag(warn_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ==============================================================
     * 2. Main container (784 x 376) — frames all 3 column panels
     * ============================================================== */
    lv_obj_t* mc = lv_obj_create(wrapper);
    lv_obj_set_size(mc, 792, TRIAD_PANEL_HEIGHT);
    lv_obj_set_pos(mc, 4, 42);
    lv_obj_remove_flag(mc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(mc, 1, 0);
    lv_obj_set_style_border_color(mc, lv_color_make(0x18, 0x18, 0x34), 0);
    lv_obj_set_style_radius(mc, 10, 0);
    lv_obj_set_style_pad_all(mc, 0, 0);
    lv_obj_set_style_shadow_width(mc, 10, 0);
    lv_obj_set_style_shadow_color(mc, lv_color_make(0x00, 0x00, 0x30), 0);
    set_gradient_bg(mc,
        lv_color_make(0x0C, 0x0C, 0x20),
        lv_color_make(0x04, 0x04, 0x12));

    /* ==============================================================
     * 3. Left panel — RAM / DISK / BATT (3 cards stacked)
     * ============================================================== */
    {
        int cw = VORTEX_LP_W - TRIAD_CARD_PADDING_H;
        int bw = cw - TRIAD_CARD_BAR_MARGIN;
        int ch = 120;   /* 3 cards: top + 3*h + 2*gap = 376 */
        int ct = TRIAD_CARD_TOP_OFFSET;
        int gap = TRIAD_CARD_GAP;

        lv_obj_t* lp = lv_obj_create(mc);
        lv_obj_set_size(lp, VORTEX_LP_W, TRIAD_PANEL_HEIGHT);
        lv_obj_set_pos(lp, 0, 0);
        lv_obj_remove_flag(lp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(lp, 0, 0);
        lv_obj_set_style_radius(lp, 8, 0);
        lv_obj_set_style_pad_all(lp, 0, 0);
        set_gradient_bg(lp,
            lv_color_make(0x12, 0x12, 0x26),
            lv_color_make(0x08, 0x08, 0x18));

        /* ---- RAM card ---- */
        {
            lv_color_t accent = th->ram;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct);

            create_icon_img(card, &icon_ram, accent, 6, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "RAM");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 40, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_ram_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            vo_ram_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* used = lv_label_create(card);
            vo_ram_swap = used;
            lv_label_set_text(used, "");
            lv_obj_set_style_text_color(used, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(used, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(used, 6, 60);

            lv_obj_t* swap = lv_label_create(card);
            vo_ram_swap2 = swap;
            lv_label_set_text(swap, "");
            lv_obj_set_style_text_color(swap, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(swap, 140, 60);
        }

        /* ---- DISK card ---- */
        {
            lv_color_t accent = th->disk;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct + (ch + gap));

            create_icon_img(card, &icon_disk, accent, 6, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "DISK");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 40, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_dsk_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            vo_dsk_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* io = lv_label_create(card);
            vo_dsk_io = io;
            lv_label_set_text(io, "");
            lv_obj_set_style_text_color(io, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(io, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(io, 6, 60);
        }

        /* ---- BATT card ---- */
        {
            lv_color_t accent = th->batt;
            lv_obj_t* card = create_card(lp, cw, ch, accent, ct + (ch + gap) * 2);

            create_icon_img(card, &icon_battery, accent, 6, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "BATT");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 40, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_bat_val = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H,
                lv_color_make(0x22, 0x22, 0x35), accent);
            vo_bat_bar = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* indicator = lv_obj_create(card);
            lv_obj_set_size(indicator, 8, 8);
            lv_obj_set_pos(indicator, 8, 70);
            lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(indicator, 0, 0);
            lv_obj_set_style_bg_color(indicator, lv_color_make(0x44, 0xDD, 0x44), 0);
            lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
            vo_bat_icon = indicator;
            lv_obj_t* stat = lv_label_create(card);
            vo_bat_sts = stat;
            lv_label_set_text(stat, "");
            lv_obj_set_style_text_color(stat, lv_color_make(0x88, 0xAA, 0x88), 0);
            lv_obj_set_style_text_font(stat, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(stat, 22, 68);
        }
    }

    /* ==============================================================
     * 4. Center panel — CPU ring gauge + GPU bar below
     * ============================================================== */
    {
        int cp_x = VORTEX_LP_W + LAYOUT_COL_GAP;   /* 176 */
        int ct = 11;    /* Center top offset for ring (shifted up 5px) */
        int ring_sz = 140;

        lv_obj_t* cp = lv_obj_create(mc);
        lv_obj_set_size(cp, VORTEX_CP_W, TRIAD_PANEL_HEIGHT);
        lv_obj_set_pos(cp, cp_x, 0);
        lv_obj_remove_flag(cp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(cp, 0, 0);
        lv_obj_set_style_radius(cp, 8, 0);
        lv_obj_set_style_pad_all(cp, 0, 0);
        set_gradient_bg(cp,
            lv_color_make(0x12, 0x12, 0x26),
            lv_color_make(0x08, 0x08, 0x18));

        /* ---- CPU Ring Gauge (Canvas) ---- */
        {
            int ring_x = (VORTEX_CP_W - ring_sz) / 2;
            vo_cpu_canvas = lv_canvas_create(cp);
            lv_obj_set_size(vo_cpu_canvas, ring_sz, ring_sz);
            lv_obj_set_pos(vo_cpu_canvas, ring_x, ct);
            lv_obj_set_style_radius(vo_cpu_canvas, ring_sz / 2, 0);
            lv_obj_set_style_clip_corner(vo_cpu_canvas, true, 0);
            /* Canvas has no border/shadow (provided by ring_frame overlay) */
            lv_obj_set_style_border_width(vo_cpu_canvas, 0, 0);
            lv_obj_set_style_shadow_width(vo_cpu_canvas, 0, 0);
            s_cpu_canvas_draw_buf = lv_draw_buf_create(ring_sz, ring_sz,
                LV_COLOR_FORMAT_ARGB8888, 0);
            if (s_cpu_canvas_draw_buf == NULL)
            {
                RTK_LOGE(TAG, "CPU canvas: lv_draw_buf_create(%dx%d) failed\n",
                    (int)ring_sz, (int)ring_sz);
            }
            else
            {
                lv_canvas_set_draw_buf(vo_cpu_canvas, s_cpu_canvas_draw_buf);
            }
            lv_obj_set_style_bg_opa(vo_cpu_canvas, LV_OPA_0, 0);
            lv_obj_set_style_bg_color(vo_cpu_canvas, lv_color_make(0x12, 0x12, 0x26), 0);

            /* Circle edge drawn directly in canvas (1px color ring + 3px edge seal),
             * no separate ring_frame widget -> zero redraw cascade flicker. */

             /* Init particle system and start 2fps animation timer */
            init_particles();

            /* Fill entire canvas with bg color (0x121226 matches card top gradient, no seam at clip boundary) */
            {
                lv_draw_buf_t* db = lv_canvas_get_draw_buf(vo_cpu_canvas);
                lv_color32_t* cp_px = (lv_color32_t*)db->data;
                lv_color32_t bg = { .red = 0x12, .green = 0x12, .blue = 0x26, .alpha = 0xFF };
                for (uint32_t i = 0; i < (uint32_t)(ring_sz * ring_sz); i++)
                    cp_px[i] = bg;
            }

            s_current_pct = 0.0f;
            s_target_pct = 0.0f;
            s_anim_phase = 0;
            if (s_particle_timer == NULL)
            {
                s_particle_timer = lv_timer_create(cpu_particle_timer_cb, 100, NULL);
            }
        }

        /* ---- Frequency & Temperature labels below ring ---- */
        {
            int fy = ct + ring_sz + 4;

            lv_obj_t* freq = lv_label_create(cp);
            vo_cpu_freq = freq;
            lv_label_set_text(freq, "Freq: N/A");
            lv_obj_set_style_text_color(freq, lv_color_make(0xAA, 0xCC, 0xEE), 0);
            lv_obj_set_style_text_font(freq, &lv_font_montserrat_18, 0);
            // lv_obj_set_pos(freq, 10, fy);
            lv_obj_align(freq, LV_ALIGN_TOP_MID, 0, fy);

            lv_obj_t* temp = lv_label_create(cp);
            vo_cpu_temp = temp;
            lv_label_set_text(temp, "Temp: N/A");
            lv_obj_set_style_text_color(temp, lv_color_make(0xAA, 0xCC, 0xEE), 0);
            lv_obj_set_style_text_font(temp, &lv_font_montserrat_18, 0);
            lv_obj_align(temp, LV_ALIGN_TOP_MID, 0, fy + 24);
            //lv_obj_set_pos(temp, 10, fy + 20);
        }

        /* ---- GPU card ---- */
        {
            lv_color_t accent = th->gpu;
            int gpu_y = ct + ring_sz + 12 + 22 + 12 + 18;  /* ~202 */
            int gpu_ch = TRIAD_PANEL_HEIGHT - gpu_y - 4;
            int cw = VORTEX_CP_W - TRIAD_CARD_PADDING_H;
            int bw = cw - TRIAD_CARD_BAR_MARGIN;
            int bh = 14;

            lv_obj_t* card = create_card(cp, cw, gpu_ch, accent, gpu_y);

            create_icon_img(card, &icon_gpu, accent, 8, 6);
            lv_obj_t* gpu_title = lv_label_create(card);
            lv_label_set_text(gpu_title, "GPU");
            lv_obj_set_style_text_color(gpu_title, accent, 0);
            lv_obj_set_style_text_font(gpu_title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(gpu_title, 44, 16);

            lv_obj_t* name = lv_label_create(card);
            vo_gpu_name = name;
            lv_label_set_text(name, "");
            lv_obj_set_style_text_color(name, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(name, 8, 44);

            lv_obj_t* val = lv_label_create(card);
            vo_gpu_val = val;
            lv_label_set_text(val, "");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 28);

            lv_obj_t* bar = create_glow_bar(card, bw, bh,
                lv_color_make(0x22, 0x22, 0x35), accent);
            vo_gpu_bar = bar;
            lv_obj_set_pos(bar, 5, 54);

            lv_obj_t* temp_mem = lv_label_create(card);
            vo_gpu_tm = temp_mem;
            lv_label_set_text(temp_mem, "");
            lv_obj_set_style_text_color(temp_mem, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp_mem, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(temp_mem, 8, 78);
        }
    }

    /* ==============================================================
     * 5. Right panel — NETWORK (compact) + SYSTEM
     * ============================================================== */
    {
        int rp_x = VORTEX_LP_W + LAYOUT_COL_GAP + VORTEX_CP_W + LAYOUT_COL_GAP;
        int cw = VORTEX_RP_W - TRIAD_CARD_PADDING_H;
        int ct = TRIAD_CARD_TOP_OFFSET;
        int gap = TRIAD_CARD_GAP;
        int ch_net = 110;   /* compact NETWORK card (was 80, too small for 3x 32px icons) */

        lv_obj_t* rp = lv_obj_create(mc);
        lv_obj_set_size(rp, VORTEX_RP_W, TRIAD_PANEL_HEIGHT);
        lv_obj_set_pos(rp, rp_x, 0);
        lv_obj_remove_flag(rp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(rp, 0, 0);
        lv_obj_set_style_radius(rp, 8, 0);
        lv_obj_set_style_pad_all(rp, 0, 0);
        set_gradient_bg(rp,
            lv_color_make(0x12, 0x12, 0x26),
            lv_color_make(0x08, 0x08, 0x18));

        /* ---- NETWORK card (compact) ---- */
        {
            lv_color_t accent = th->net;
            lv_obj_t* card = create_card(rp, cw, ch_net, accent, ct);

            create_icon_img(card, &icon_wifi, accent, 6, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "NETWORK");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 40, 16);

            /* TX */
            create_icon_img(card, &icon_arrow_up,
                lv_color_make(0x88, 0xDD, 0xAA), 8, 35);
            lv_obj_t* up_lbl = lv_label_create(card);
            vo_net_tx = up_lbl;
            lv_label_set_text(up_lbl, "");
            lv_obj_set_style_text_color(up_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(up_lbl, 48, 46);

            /* RX */
            create_icon_img(card, &icon_arrow_down,
                lv_color_make(0x88, 0xDD, 0xAA), 8, 69);
            lv_obj_t* down_lbl = lv_label_create(card);
            vo_net_rx = down_lbl;
            lv_label_set_text(down_lbl, "");
            lv_obj_set_style_text_color(down_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(down_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(down_lbl, 48, 80);
        }

        /* ---- SYSTEM card ---- */
        {
            int ch_sys = TRIAD_PANEL_HEIGHT - ct - ch_net - gap - 4;
            lv_color_t accent = th->sys;
            lv_obj_t* card = create_card(rp, cw, ch_sys, accent, ct + ch_net + gap);

            create_icon_img(card, &icon_gear, accent, 6, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "SYSTEM");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 40, 16);

            int lx = 6;
            int ly = 50;
            int lgap = 38;

            /* Processes */
            create_icon_img(card, &icon_list,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11);
            lv_obj_t* proc_lbl = lv_label_create(card);
            vo_sys_p = proc_lbl;
            lv_label_set_text(proc_lbl, "");
            lv_obj_set_style_text_color(proc_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(proc_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(proc_lbl, lx + 40, ly);

            /* Cores */
            create_icon_img(card, &icon_cpu,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap);
            lv_obj_t* cores_lbl = lv_label_create(card);
            vo_sys_c = cores_lbl;
            lv_label_set_text(cores_lbl, "");
            lv_obj_set_style_text_color(cores_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(cores_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(cores_lbl, lx + 40, ly + lgap);

            /* Boot time */
            create_icon_img(card, &icon_power_off,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 2);
            lv_obj_t* boot_lbl = lv_label_create(card);
            vo_sys_b = boot_lbl;
            lv_label_set_text(boot_lbl, "");
            lv_obj_set_style_text_color(boot_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(boot_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(boot_lbl, lx + 40, ly + lgap * 2 - 2);

            /* Hostname */
            create_icon_img(card, &icon_user,
                lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 3);
            lv_obj_t* host_lbl = lv_label_create(card);
            vo_sys_h = host_lbl;
            lv_label_set_text(host_lbl, "");
            lv_obj_set_style_text_color(host_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(host_lbl, lx + 40, ly + lgap * 3 - 2);

            /* OS */
            create_icon_img(card, &icon_globe,
                lv_color_make(0x88, 0xAA, 0xCC), lx, ly - 11 + lgap * 4);
            lv_obj_t* os_lbl = lv_label_create(card);
            vo_sys_o = os_lbl;
            lv_label_set_text(os_lbl, "OS: N/A");
            lv_obj_set_style_text_color(os_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(os_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_width(os_lbl, cw - 24);
            lv_obj_set_pos(os_lbl, lx + 40, ly + lgap * 4 - 2);
        }
    }

    /* ==============================================================
     * 6. Env bar — temperature & humidity
     * ============================================================== */
    {
        int bar_w = 792;
        int bar_h = 32;
        int bx = 4;
        int by = 420;

        lv_obj_t* bar = lv_obj_create(wrapper);
        lv_obj_set_size(bar, bar_w, bar_h);
        lv_obj_set_pos(bar, bx, by);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_make(0x11, 0x44, 0x33), 0);
        lv_obj_set_style_radius(bar, 8, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_shadow_width(bar, 6, 0);
        lv_obj_set_style_shadow_color(bar, lv_color_make(0x00, 0x20, 0x10), 0);
        set_gradient_bg(bar,
            lv_color_make(0x0A, 0x22, 0x16),
            lv_color_make(0x06, 0x14, 0x0C));

        create_icon_img(bar, &icon_temp, th->env, 12, 0);
        lv_obj_t* env_title = lv_label_create(bar);
        lv_label_set_text(env_title, "ENV");
        lv_obj_set_style_text_color(env_title, th->env, 0);
        lv_obj_set_style_text_font(env_title, &lv_font_montserrat_18, 0);
        lv_obj_align(env_title, LV_ALIGN_LEFT_MID, 46, 0);

        lv_obj_t* temp_lbl = lv_label_create(bar);
        vo_env_t = temp_lbl;
        lv_label_set_text(temp_lbl, "Temp: --. \xC2\xB0\x43 / --. \xC2\xB0\x46");
        lv_obj_set_style_text_color(temp_lbl, lv_color_make(0xAA, 0xFF, 0xCC), 0);
        lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 180, 0);

        lv_obj_t* humi_lbl = lv_label_create(bar);
        vo_env_h = humi_lbl;
        lv_label_set_text(humi_lbl, "Humi: --.-%");
        lv_obj_set_style_text_color(humi_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
        lv_obj_set_style_text_font(humi_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(humi_lbl, LV_ALIGN_LEFT_MID, 580, 0);
    }

    /* ==============================================================
     * 7. Footer — status bar
     * ============================================================== */
    {
        lv_obj_t* footer = lv_obj_create(wrapper);
        lv_obj_set_size(footer, SCREEN_WIDTH, 22);
        lv_obj_set_pos(footer, 0, 458);
        lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_style_border_width(footer, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(footer, lv_color_make(0x1A, 0x1A, 0x3A), 0);
        lv_obj_set_style_pad_all(footer, 0, 0);
        lv_obj_set_style_radius(footer, 0, 0);
        set_gradient_bg(footer,
            lv_color_make(0x0E, 0x0E, 0x28),
            lv_color_make(0x06, 0x06, 0x16));

        lv_obj_t* ftr_lbl = lv_label_create(footer);
        lv_label_set_text(ftr_lbl,
            " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
        lv_obj_set_style_text_color(ftr_lbl, lv_color_make(0x66, 0x88, 0xAA), 0);
        lv_obj_set_style_text_font(ftr_lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ftr_lbl, LV_ALIGN_LEFT_MID, 15, 0);
        g_mqtt_status_label = ftr_lbl;   /* Dynamic MQTT status update */
    }
}

/* ========================================================================
 * Layout C — PULSE (2-row x 3-column HUD grid)
 * ========================================================================
 *
 *   Header:         800x36   at (0,0)
 *   Main container: 784x376  at (8,42)
 *     Row 1 (3 boxes):   CPU / RAM / DISK
 *       each ~256w x 168h, at y=4, gap 8px
 *     Sys info bar:      at y=178  (28px tall)
 *     Row 2 (3 boxes):   BATT / GPU / NET
 *       each ~256w x 164h, at y=212, gap 8px
 *   Env bar:        784x32   at (8,420)
 *   Footer:         800x22   at (0,458)
 *
 *   Box style: transparent bg, 2px theme-colored border, glow shadow
 *   Each box: large centered percentage, name label, sub-info
 * ======================================================================== */
void create_layout_pulse(lv_obj_t* parent)
{
    const theme_t* th = &g_themes[g_theme_id];
    const char* tname = theme_get_name(g_theme_id);
    const char* lname = layout_get_name(LAYOUT_PULSE);

    /* Box / bar dimensions */
#define PULSE_BOX_W     260
#define PULSE_BOW_GAP   6
#define PULSE_ROW1_H    168
#define PULSE_ROW2_H    156
#define PULSE_SYS_H     36

    /* Box x positions (3 boxes, 260w + 6gap, centred in 792-wide mc) */
#define PULSE_BOX0_X    0
#define PULSE_BOX1_X    266     /* 0 + 260 + 6 */
#define PULSE_BOX2_X    530     /* 266 + 260 + 6 */

    /* ----- Wrapper container (for clean destroy) ----- */
    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(wrapper, 0, 0);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_radius(wrapper, 0, 0);
    lv_obj_set_style_bg_opa(wrapper, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_remove_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);
    set_layout_container(wrapper);

    RTK_LOGI(TAG, "create_layout_pulse\n");

    /* ==============================================================
     * 1. Header — branded title, user, clock, NO DATA warning
     * ============================================================== */
    lv_obj_t* header = lv_obj_create(wrapper);
    lv_obj_set_size(header, SCREEN_WIDTH, 36);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_make(0x1A, 0x1A, 0x3A), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_shadow_width(header, 10, 0);
    lv_obj_set_style_shadow_color(header, lv_color_make(0x00, 0x00, 0x30), 0);
    set_gradient_bg(header, th->bg_top, th->bg_bot);

    /* Gear icon (title) */
    create_icon_img(header, &icon_gear, th->header, 12, 2);

    /* Branded title */
    {
        char title_buf[64];
        snprintf(title_buf, sizeof(title_buf),
            "PC DASHBOARD - %s - %s", lname, tname);
        lv_obj_t* title_lbl = lv_label_create(header);
        lv_label_set_text(title_lbl, title_buf);
        lv_obj_set_style_text_color(title_lbl, th->header, 0);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 52, 0);
    }

    /* User name label */
    create_icon_img(header, &icon_user,
        lv_color_make(0x88, 0xAA, 0xCC), 430, 2);
    lv_obj_t* user_lbl = lv_label_create(header);
    pu_user = user_lbl;
    lv_label_set_text(user_lbl, "");
    lv_obj_set_style_text_color(user_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(user_lbl, 460, 4);

    /* Clock label */
    lv_obj_t* clock_lbl = lv_label_create(header);
    pu_time = clock_lbl;
    lv_label_set_text(clock_lbl, "--:--:--");
    lv_obj_set_style_text_color(clock_lbl, lv_color_make(0x00, 0xFF, 0x88), 0);
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -2, 0);

    /* NO DATA warning */
    {
        lv_obj_t* warn_icon = create_icon_img(header, &icon_warning,
            th->warn, 680, 2);
        pu_warn_icon = warn_icon;
        lv_obj_add_flag(warn_icon, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* warn_lbl = lv_label_create(header);
        pu_warn_lbl = warn_lbl;
        lv_label_set_text(warn_lbl, " NO DATA");
        lv_obj_set_style_text_color(warn_lbl, th->warn, 0);
        lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(warn_lbl, LV_ALIGN_RIGHT_MID, -130, 0);
        lv_obj_add_flag(warn_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* ==============================================================
     * 2. Main container (784 x 376)
     * ============================================================== */
    lv_obj_t* mc = lv_obj_create(wrapper);
    lv_obj_set_size(mc, 792, TRIAD_PANEL_HEIGHT);
    lv_obj_set_pos(mc, 4, 42);
    lv_obj_remove_flag(mc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(mc, 1, 0);
    lv_obj_set_style_border_color(mc, lv_color_make(0x18, 0x18, 0x34), 0);
    lv_obj_set_style_radius(mc, 10, 0);
    lv_obj_set_style_pad_all(mc, 0, 0);
    lv_obj_set_style_shadow_width(mc, 10, 0);
    lv_obj_set_style_shadow_color(mc, lv_color_make(0x00, 0x00, 0x30), 0);
    set_gradient_bg(mc,
        lv_color_make(0x0C, 0x0C, 0x20),
        lv_color_make(0x04, 0x04, 0x12));

    /* ==============================================================
     * 3. Row 1 — CPU, RAM, DISK
     * ============================================================== */
    {
        int box_y = 4;

        /* ---- CPU box ---- */
        {
            lv_color_t accent = th->cpu;
            lv_obj_t* box = lv_obj_create(mc);
            lv_obj_set_size(box, PULSE_BOX_W, PULSE_ROW1_H);
            lv_obj_set_pos(box, PULSE_BOX0_X, box_y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, accent, 0);
            lv_obj_set_style_radius(box, 8, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_0, 0);
            lv_obj_set_style_shadow_width(box, 8, 0);
            lv_obj_set_style_shadow_color(box, accent, 0);
            lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

            create_icon_img(box, &icon_cpu, accent, 10, 8);

            lv_obj_t* pct = lv_label_create(box);
            pu_cpu_val = pct;
            lv_label_set_text(pct, "0%");
            lv_obj_set_style_text_color(pct, accent, 0);
            lv_obj_set_style_text_font(pct, &lv_font_montserrat_32, 0);
            lv_obj_center(pct);

            lv_obj_t* lbl = lv_label_create(box);
            lv_label_set_text(lbl, "CPU");
            lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(lbl, 52, 14);

            lv_obj_t* sub = lv_label_create(box);
            pu_cpu_sub = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);

            lv_obj_t* temp = lv_label_create(box);
            pu_cpu_temp = temp;
            lv_label_set_text(temp, "");
            lv_obj_set_style_text_color(temp, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);
            lv_obj_align(temp, LV_ALIGN_CENTER, 0, 55);
        }

        /* ---- RAM box ---- */
        {
            lv_color_t accent = th->ram;
            lv_obj_t* box = lv_obj_create(mc);
            lv_obj_set_size(box, PULSE_BOX_W, PULSE_ROW1_H);
            lv_obj_set_pos(box, PULSE_BOX1_X, box_y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, accent, 0);
            lv_obj_set_style_radius(box, 8, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_0, 0);
            lv_obj_set_style_shadow_width(box, 8, 0);
            lv_obj_set_style_shadow_color(box, accent, 0);
            lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

            create_icon_img(box, &icon_ram, accent, 10, 8);

            lv_obj_t* pct = lv_label_create(box);
            pu_ram_val = pct;
            lv_label_set_text(pct, "0%");
            lv_obj_set_style_text_color(pct, accent, 0);
            lv_obj_set_style_text_font(pct, &lv_font_montserrat_32, 0);
            lv_obj_center(pct);

            lv_obj_t* lbl = lv_label_create(box);
            lv_label_set_text(lbl, "RAM");
            lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(lbl, 52, 14);

            lv_obj_t* sub = lv_label_create(box);
            pu_ram_sub = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);

            lv_obj_t* swap2 = lv_label_create(box);
            pu_ram_swap2 = swap2;
            lv_label_set_text(swap2, "");
            lv_obj_set_style_text_color(swap2, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap2, &lv_font_montserrat_14, 0);
            lv_obj_align(swap2, LV_ALIGN_CENTER, 0, 55);
        }

        /* ---- DISK box ---- */
        {
            lv_color_t accent = th->disk;
            lv_obj_t* box = lv_obj_create(mc);
            lv_obj_set_size(box, PULSE_BOX_W, PULSE_ROW1_H);
            lv_obj_set_pos(box, PULSE_BOX2_X, box_y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, accent, 0);
            lv_obj_set_style_radius(box, 8, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_0, 0);
            lv_obj_set_style_shadow_width(box, 8, 0);
            lv_obj_set_style_shadow_color(box, accent, 0);
            lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

            create_icon_img(box, &icon_disk, accent, 10, 8);

            lv_obj_t* pct = lv_label_create(box);
            pu_dsk_val = pct;
            lv_label_set_text(pct, "0%");
            lv_obj_set_style_text_color(pct, accent, 0);
            lv_obj_set_style_text_font(pct, &lv_font_montserrat_32, 0);
            lv_obj_center(pct);

            lv_obj_t* lbl = lv_label_create(box);
            lv_label_set_text(lbl, "DISK");
            lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(lbl, 52, 14);

            lv_obj_t* sub = lv_label_create(box);
            pu_dsk_sub = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }
    }

    /* ==============================================================
     * 4. System info bar — processes, cores, boot time, OS
     * ============================================================== */
    {
        int sy = 176;
        int sw = 792;
        int sh = PULSE_SYS_H;

        lv_obj_t* sys_bar = lv_obj_create(mc);
        lv_obj_set_size(sys_bar, sw, sh);
        lv_obj_set_pos(sys_bar, 0, sy);
        lv_obj_remove_flag(sys_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(sys_bar, 0, 0);
        lv_obj_set_style_radius(sys_bar, 4, 0);
        lv_obj_set_style_pad_all(sys_bar, 0, 0);
        lv_obj_set_style_border_width(sys_bar, 1, 0);
        lv_obj_set_style_border_color(sys_bar, lv_color_make(0x22, 0x22, 0x44), 0);
        set_gradient_bg(sys_bar,
            lv_color_make(0x0A, 0x0A, 0x1E),
            lv_color_make(0x06, 0x06, 0x14));

        lv_color_t sys_col = lv_color_make(0x88, 0xBB, 0xCC);
        lv_color_t sys_val = lv_color_make(0xAA, 0xDD, 0xEE);

        /* Processes — short, compact */
        {
            create_icon_img(sys_bar, &icon_list, sys_col, 10, 2);
            lv_obj_t* pd = lv_label_create(sys_bar);
            pu_sys_p = pd;
            lv_label_set_text(pd, "Procs: --");
            lv_obj_set_style_text_color(pd, sys_val, 0);
            lv_obj_set_style_text_font(pd, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(pd, 50, 13);
        }

        /* Cores */
        {
            create_icon_img(sys_bar, &icon_cpu, sys_col, 210, 2);
            lv_obj_t* cd = lv_label_create(sys_bar);
            pu_sys_c = cd;
            lv_label_set_text(cd, "Cores: --");
            lv_obj_set_style_text_color(cd, sys_val, 0);
            lv_obj_set_style_text_font(cd, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(cd, 250, 13);
        }

        /* Boot time — longest, most space */
        {
            create_icon_img(sys_bar, &icon_power_off, sys_col, 410, 2);
            lv_obj_t* bd = lv_label_create(sys_bar);
            pu_sys_b = bd;
            lv_label_set_text(bd, "Boot: --");
            lv_obj_set_style_text_color(bd, sys_val, 0);
            lv_obj_set_style_text_font(bd, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(bd, 448, 13);
        }

        /* OS */
        {
            create_icon_img(sys_bar, &icon_globe, sys_col, 630, 2);
            lv_obj_t* od = lv_label_create(sys_bar);
            pu_sys_o = od;
            lv_label_set_text(od, "OS: --");
            lv_obj_set_style_text_color(od, sys_val, 0);
            lv_obj_set_style_text_font(od, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(od, 668, 13);
        }
    }

    /* ==============================================================
     * 5. Row 2 — BATT, GPU, NET
     * ============================================================== */
    {
        int box_y = 216;

        /* ---- BATT box ---- */
        {
            lv_color_t accent = th->batt;
            lv_obj_t* box = lv_obj_create(mc);
            lv_obj_set_size(box, PULSE_BOX_W, PULSE_ROW2_H);
            lv_obj_set_pos(box, PULSE_BOX0_X, box_y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, accent, 0);
            lv_obj_set_style_radius(box, 8, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_0, 0);
            lv_obj_set_style_shadow_width(box, 8, 0);
            lv_obj_set_style_shadow_color(box, accent, 0);
            lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

            create_icon_img(box, &icon_battery, accent, 10, 8);

            lv_obj_t* pct = lv_label_create(box);
            pu_bat_val = pct;
            lv_label_set_text(pct, "0%");
            lv_obj_set_style_text_color(pct, accent, 0);
            lv_obj_set_style_text_font(pct, &lv_font_montserrat_32, 0);
            lv_obj_center(pct);

            lv_obj_t* lbl = lv_label_create(box);
            lv_label_set_text(lbl, "BATT");
            lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(lbl, 52, 14);

            lv_obj_t* sub = lv_label_create(box);
            pu_bat_sub = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }

        /* ---- GPU box ---- */
        {
            lv_color_t accent = th->gpu;
            lv_obj_t* box = lv_obj_create(mc);
            lv_obj_set_size(box, PULSE_BOX_W, PULSE_ROW2_H);
            lv_obj_set_pos(box, PULSE_BOX1_X, box_y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, accent, 0);
            lv_obj_set_style_radius(box, 8, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_0, 0);
            lv_obj_set_style_shadow_width(box, 8, 0);
            lv_obj_set_style_shadow_color(box, accent, 0);
            lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

            create_icon_img(box, &icon_gpu, accent, 10, 8);

            lv_obj_t* pct = lv_label_create(box);
            pu_gpu_val = pct;
            lv_label_set_text(pct, "0%");
            lv_obj_set_style_text_color(pct, accent, 0);
            lv_obj_set_style_text_font(pct, &lv_font_montserrat_32, 0);
            lv_obj_center(pct);

            lv_obj_t* lbl = lv_label_create(box);
            lv_label_set_text(lbl, "GPU");
            lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(lbl, 52, 14);

            lv_obj_t* sub = lv_label_create(box);
            pu_gpu_sub = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }

        /* ---- NET box ---- */
        {
            lv_color_t accent = th->net;
            lv_obj_t* box = lv_obj_create(mc);
            lv_obj_set_size(box, PULSE_BOX_W, PULSE_ROW2_H);
            lv_obj_set_pos(box, PULSE_BOX2_X, box_y);
            lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, accent, 0);
            lv_obj_set_style_radius(box, 8, 0);
            lv_obj_set_style_pad_all(box, 0, 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_0, 0);
            lv_obj_set_style_shadow_width(box, 8, 0);
            lv_obj_set_style_shadow_color(box, accent, 0);
            lv_obj_set_style_shadow_opa(box, LV_OPA_40, 0);

            create_icon_img(box, &icon_wifi, accent, 10, 8);

            lv_obj_t* pct = lv_label_create(box);
            lv_label_set_text(pct, "0%");
            lv_obj_set_style_text_color(pct, accent, 0);
            lv_obj_set_style_text_font(pct, &lv_font_montserrat_32, 0);
            lv_obj_center(pct);

            lv_obj_t* lbl = lv_label_create(box);
            lv_label_set_text(lbl, "NETWORK");
            lv_obj_set_style_text_color(lbl, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(lbl, 52, 14);

            lv_obj_t* sub = lv_label_create(box);
            pu_net_sub = sub;
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }
    }

    /* ==============================================================
     * 6. Env bar — temperature & humidity
     * ============================================================== */
    {
        int bar_w = 792;
        int bar_h = 32;
        int bx = 4;
        int by = 420;

        lv_obj_t* bar = lv_obj_create(wrapper);
        lv_obj_set_size(bar, bar_w, bar_h);
        lv_obj_set_pos(bar, bx, by);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_make(0x11, 0x44, 0x33), 0);
        lv_obj_set_style_radius(bar, 8, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_shadow_width(bar, 6, 0);
        lv_obj_set_style_shadow_color(bar, lv_color_make(0x00, 0x20, 0x10), 0);
        set_gradient_bg(bar,
            lv_color_make(0x0A, 0x22, 0x16),
            lv_color_make(0x06, 0x14, 0x0C));

        create_icon_img(bar, &icon_temp, th->env, 12, 0);
        lv_obj_t* env_title = lv_label_create(bar);
        lv_label_set_text(env_title, "ENV");
        lv_obj_set_style_text_color(env_title, th->env, 0);
        lv_obj_set_style_text_font(env_title, &lv_font_montserrat_18, 0);
        lv_obj_align(env_title, LV_ALIGN_LEFT_MID, 46, 0);

        lv_obj_t* temp_lbl = lv_label_create(bar);
        pu_env_t = temp_lbl;
        lv_label_set_text(temp_lbl, "Temp: --. \xC2\xB0\x43 / --. \xC2\xB0\x46");
        lv_obj_set_style_text_color(temp_lbl, lv_color_make(0xAA, 0xFF, 0xCC), 0);
        lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 180, 0);

        lv_obj_t* humi_lbl = lv_label_create(bar);
        pu_env_h = humi_lbl;
        lv_label_set_text(humi_lbl, "Humi: --.-%");
        lv_obj_set_style_text_color(humi_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
        lv_obj_set_style_text_font(humi_lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(humi_lbl, LV_ALIGN_LEFT_MID, 580, 0);
    }

    /* ==============================================================
     * 7. Footer — status bar
     * ============================================================== */
    {
        lv_obj_t* footer = lv_obj_create(wrapper);
        lv_obj_set_size(footer, SCREEN_WIDTH, 22);
        lv_obj_set_pos(footer, 0, 458);
        lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_style_border_width(footer, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(footer, lv_color_make(0x1A, 0x1A, 0x3A), 0);
        lv_obj_set_style_pad_all(footer, 0, 0);
        lv_obj_set_style_radius(footer, 0, 0);
        set_gradient_bg(footer,
            lv_color_make(0x0E, 0x0E, 0x28),
            lv_color_make(0x06, 0x06, 0x16));

        lv_obj_t* ftr_lbl = lv_label_create(footer);
        char ftr_buf[80];
        snprintf(ftr_buf, sizeof(ftr_buf),
            " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
        lv_label_set_text(ftr_lbl, ftr_buf);
        lv_obj_set_style_text_color(ftr_lbl, lv_color_make(0x66, 0x88, 0xAA), 0);
        lv_obj_set_style_text_font(ftr_lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ftr_lbl, LV_ALIGN_LEFT_MID, 15, 0);
        g_mqtt_status_label = ftr_lbl;   /* Dynamic MQTT status update */
    }

#undef PULSE_BOX_W
#undef PULSE_BOW_GAP
#undef PULSE_ROW1_H
#undef PULSE_ROW2_H
#undef PULSE_SYS_H
#undef PULSE_BOX0_X
#undef PULSE_BOX1_X
#undef PULSE_BOX2_X
}

/* ========================================================================
 * V3 update helpers — clock display
 * ======================================================================== */
static void update_clock_v3(lv_obj_t* time_label)
{
    if (time_label == NULL)
        return;

    if (g_time_base_ts == 0)
    {
        lv_label_set_text(time_label, "--:--:--");
        return;
    }

    uint32_t now_ms = rtos_time_get_current_system_time_ms();
    uint32_t elapsed_s = (now_ms - g_time_base_ms) / 1000;
    uint32_t current_ts = g_time_base_ts + elapsed_s + UTC8_OFFSET_SEC;

    uint16_t y;
    uint8_t mo, d, h, mi, s;
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
        (int)y, (int)mo, (int)d, (int)h, (int)mi, (int)s);
}

/* ========================================================================
 * V3 update — Layout A / TRIAD
 * ======================================================================== */
void update_layout_triad(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    if (!stats.has_data)
        return;

    /* ---- CPU ---- */
    if (tr_cpu_val || tr_cpu_bar)
    {
        int val = (int)(stats.cpu + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_cpu_pct)
        {
            lv_color_t c = heat_color(stats.cpu);
            if (tr_cpu_val)
            {
                lv_label_set_text_fmt(tr_cpu_val, "%d%%", val);
                lv_obj_set_style_text_color(tr_cpu_val, c, 0);
            }
            if (tr_cpu_bar)
            {
                lv_bar_set_value(tr_cpu_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(tr_cpu_bar, c, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(tr_cpu_bar, c, LV_PART_INDICATOR);
            }
            s_last_cpu_pct = val;
        }
    }
    if (tr_cpu_freq)
    {
        float freq = stats.cpu_freq_current;
        if (freq != s_last_cpu_freq)
        {
            if (stats.cpu_freq_current > 0)
                lv_label_set_text_fmt(tr_cpu_freq, "Freq: %.0f MHz", freq);
            else if (stats.cpu_freq_max > 0)
                lv_label_set_text_fmt(tr_cpu_freq, "Freq: up to %.0f MHz", stats.cpu_freq_max);
            else
                lv_label_set_text(tr_cpu_freq, "Freq: N/A");
            s_last_cpu_freq = freq;
        }
    }
    if (tr_cpu_temp)
    {
        float temp = stats.cpu_temp;
        if (temp != s_last_cpu_temp)
        {
            if (stats.cpu_temp_valid)
                lv_label_set_text_fmt(tr_cpu_temp, "Temp: %.1f \xC2\xB0\x43", temp);
            else
                lv_label_set_text(tr_cpu_temp, "Temp: N/A");
            s_last_cpu_temp = temp;
        }
    }

    /* Flash alert: CPU >80% or temp >70C -> blink value+bar (fast flash handles card) */
    {
        s_cpu_data_seen = s_cpu_data_seen || (stats.cpu > 0) || stats.cpu_temp_valid;
        bool over = s_cpu_data_seen && ((stats.cpu > g_flash_threshold.cpu_pct) || (stats.cpu_temp_valid && stats.cpu_temp > g_flash_threshold.cpu_temp_c));
        g_cpu_over = over;
        lv_opa_t opa = (over && !g_flash_on) ? LV_OPA_40 : LV_OPA_COVER;
        if (tr_cpu_val) lv_obj_set_style_text_opa(tr_cpu_val, opa, 0);
        if (tr_cpu_bar) lv_obj_set_style_bg_opa(tr_cpu_bar, opa, LV_PART_INDICATOR);
    }

    /* ---- RAM ---- */
    if (tr_ram_val || tr_ram_bar)
    {
        int val = (int)(stats.mem + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_ram_pct)
        {
            lv_color_t c = heat_color(stats.mem);
            if (tr_ram_val)
            {
                lv_label_set_text_fmt(tr_ram_val, "%d%%", val);
                lv_obj_set_style_text_color(tr_ram_val, c, 0);
            }
            if (tr_ram_bar)
            {
                lv_bar_set_value(tr_ram_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(tr_ram_bar, c, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(tr_ram_bar, c, LV_PART_INDICATOR);
            }
            s_last_ram_pct = val;
        }
    }
    if (tr_ram_swap && stats.mem_total > 0)
    {
        if (stats.mem_used != s_last_mem_used || stats.mem_total != s_last_mem_total)
        {
            char used_str[16], total_str[16];
            format_bytes(stats.mem_used, used_str, sizeof(used_str));
            format_bytes(stats.mem_total, total_str, sizeof(total_str));
            lv_label_set_text_fmt(tr_ram_swap, "%s / %s", used_str, total_str);
            s_last_mem_used = stats.mem_used;
            s_last_mem_total = stats.mem_total;
        }
    }
    if (tr_ram_swap2)
    {
        float swap = stats.swap_percent;
        if (swap != s_last_swap_pct)
        {
            if (stats.swap_percent >= 0)
                lv_label_set_text_fmt(tr_ram_swap2, "Swap: %.1f%%", swap);
            else
                lv_label_set_text(tr_ram_swap2, "");
            s_last_swap_pct = swap;
        }
    }

    /* ---- DISK ---- */
    if (tr_dsk_val || tr_dsk_bar)
    {
        int val = (int)(stats.disk + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_dsk_pct)
        {
            lv_color_t c = heat_color(stats.disk);
            if (tr_dsk_val)
            {
                lv_label_set_text_fmt(tr_dsk_val, "%d%%", val);
                lv_obj_set_style_text_color(tr_dsk_val, c, 0);
            }
            if (tr_dsk_bar)
            {
                lv_bar_set_value(tr_dsk_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(tr_dsk_bar, c, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(tr_dsk_bar, c, LV_PART_INDICATOR);
            }
            s_last_dsk_pct = val;
        }
    }
    if (tr_dsk_io)
    {
        float io_val = (stats.disk_io_percent >= 0) ? stats.disk_io_percent : -1.0f;
        if (io_val != s_last_dsk_io)
        {
            if (stats.disk_io_percent >= 0)
                lv_label_set_text_fmt(tr_dsk_io, "IO Util: %.1f%%", stats.disk_io_percent);
            else
                lv_label_set_text(tr_dsk_io, "IO Util: N/A");
            s_last_dsk_io = io_val;
        }
    }

    /* ---- BATT ---- */
    if (tr_bat_val || tr_bat_bar)
    {
        int val = (int)(stats.battery_percent + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_bat_pct)
        {
            lv_color_t bc;
            if (stats.battery_percent > 50)
                bc = lv_color_make(0x44, 0xCC, 0x44);
            else if (stats.battery_percent > 20)
                bc = lv_color_make(0xFF, 0xAA, 0x00);
            else
                bc = lv_color_make(0xFF, 0x33, 0x33);

            if (tr_bat_val)
            {
                if (stats.battery_percent > 0)
                {
                    lv_label_set_text_fmt(tr_bat_val, "%d%%", val);
                    lv_obj_set_style_text_color(tr_bat_val, bc, 0);
                }
                else
                {
                    lv_label_set_text(tr_bat_val, "N/A");
                    lv_obj_set_style_text_color(tr_bat_val, lv_color_make(0x66, 0x66, 0x88), 0);
                }
            }
            if (tr_bat_bar)
            {
                lv_bar_set_value(tr_bat_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(tr_bat_bar, bc, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(tr_bat_bar, bc, LV_PART_INDICATOR);
            }
            s_last_bat_pct = val;
        }
    }
    if (tr_bat_sts)
    {
        bool plugged = stats.battery_plugged;
        if (plugged != s_last_bat_plugged)
        {
            if (stats.battery_plugged)
                lv_label_set_text(tr_bat_sts, "Plugged In");
            else if (stats.battery_percent > 0)
                lv_label_set_text(tr_bat_sts, "On Battery");
            else
                lv_label_set_text(tr_bat_sts, "No Battery");
            s_last_bat_plugged = plugged;

            /* Update battery dot color on change - plugged=green, battery=red */
            if (tr_bat_icon)
            {
                if (plugged)
                    lv_obj_set_style_bg_color(tr_bat_icon,
                        lv_color_make(0x44, 0xDD, 0x44), 0);  /* green */
                else
                    lv_obj_set_style_bg_color(tr_bat_icon,
                        lv_color_make(0xFF, 0x33, 0x33), 0);  /* red */
            }
        }
    }

    /* ---- GPU ---- */
    if (tr_gpu_name)
    {
        if (strcmp(stats.gpu_name, s_last_gpu_name) != 0)
        {
            if (strlen(stats.gpu_name) > 0)
                lv_label_set_text(tr_gpu_name, stats.gpu_name);
            else
                lv_label_set_text(tr_gpu_name, "GPU: N/A");
            strncpy(s_last_gpu_name, stats.gpu_name, sizeof(s_last_gpu_name) - 1);
        }
    }
    if (tr_gpu_val || tr_gpu_bar)
    {
        float usage = stats.gpu_usage;
        if (usage != s_last_gpu_usage)
        {
            if (stats.gpu_usage >= 0 && strlen(stats.gpu_name) > 0)
            {
                int gval = (int)(stats.gpu_usage + 0.5f);
                if (gval > 100) gval = 100;
                lv_color_t gc = heat_color(stats.gpu_usage);

                if (tr_gpu_val)
                {
                    lv_label_set_text_fmt(tr_gpu_val, "%d%%", gval);
                    lv_obj_set_style_text_color(tr_gpu_val, gc, 0);
                }
                if (tr_gpu_bar)
                {
                    lv_bar_set_value(tr_gpu_bar, gval, LV_ANIM_ON);
                    lv_obj_set_style_bg_color(tr_gpu_bar, gc, LV_PART_INDICATOR);
                    lv_obj_set_style_shadow_color(tr_gpu_bar, gc, LV_PART_INDICATOR);
                }
            }
            else
            {
                if (tr_gpu_val)
                {
                    lv_label_set_text(tr_gpu_val, "N/A");
                    lv_obj_set_style_text_color(tr_gpu_val, lv_color_make(0x66, 0x66, 0x88), 0);
                }
                if (tr_gpu_bar)
                    lv_bar_set_value(tr_gpu_bar, 0, LV_ANIM_ON);
            }
            s_last_gpu_usage = usage;
        }
    }
    if (tr_gpu_tm)
    {
        if (strlen(stats.gpu_name) > 0)
        {
            char temp_str[32] = "N/A";
            char mem_str[48] = "N/A";
            if (stats.gpu_temp_c >= 0)
                snprintf(temp_str, sizeof(temp_str), "%.0f \xC2\xB0\x43", stats.gpu_temp_c);
            if (stats.gpu_mem_used_mb >= 0 && stats.gpu_mem_total_mb >= 0)
                snprintf(mem_str, sizeof(mem_str), "%.0f/%.0f MB",
                    stats.gpu_mem_used_mb, stats.gpu_mem_total_mb);
            else if (stats.gpu_mem_used_mb >= 0)
                snprintf(mem_str, sizeof(mem_str), "%.0f MB used", stats.gpu_mem_used_mb);
            lv_label_set_text_fmt(tr_gpu_tm, "%s  %s", temp_str, mem_str);
        }
        else
        {
            lv_label_set_text(tr_gpu_tm, "No GPU data available");
        }
    }

    /* ---- DISK I/O ---- */
    if (tr_io_read || tr_io_write)
    {
        if (stats.disk_read_bytes != s_last_io_read || stats.disk_write_bytes != s_last_io_write)
        {
            char read_str[24], write_str[24];
            format_bytes(stats.disk_read_bytes, read_str, sizeof(read_str));
            format_bytes(stats.disk_write_bytes, write_str, sizeof(write_str));
            if (tr_io_read)
                lv_label_set_text_fmt(tr_io_read, "Read:  %s", read_str);
            if (tr_io_write)
                lv_label_set_text_fmt(tr_io_write, "Write: %s", write_str);
            s_last_io_read = stats.disk_read_bytes;
            s_last_io_write = stats.disk_write_bytes;
        }
    }

    /* ---- NETWORK ---- */
    if (tr_net_tx)
    {
        float tx = stats.net_upload_kbps;
        if (tx != s_last_net_tx)
        {
            lv_label_set_text_fmt(tr_net_tx, "TX: %.1f KB/s", tx);
            s_last_net_tx = tx;
        }
    }
    if (tr_net_rx)
    {
        float rx = stats.net_download_kbps;
        if (rx != s_last_net_rx)
        {
            lv_label_set_text_fmt(tr_net_rx, "RX: %.1f KB/s", rx);
            s_last_net_rx = rx;
        }
    }

    /* ---- SYSTEM ---- */
    if (tr_sys_p)
    {
        uint32_t pc = stats.process_count;
        if (pc != s_last_proc_cnt)
        {
            lv_label_set_text_fmt(tr_sys_p, "Processes: %d", (int)pc);
            s_last_proc_cnt = pc;
        }
    }
    if (tr_sys_c)
    {
        uint8_t cores = stats.cpu_cores_physical;
        if (cores != s_last_cores)
        {
            lv_label_set_text_fmt(tr_sys_c, "Cores: %dP / %dL",
                (int)stats.cpu_cores_physical,
                (int)stats.cpu_cores_logical);
            s_last_cores = cores;
        }
    }
    if (tr_sys_b)
    {
        uint32_t bt = stats.boot_time;
        if (bt != s_last_boot_time)
        {
            s_last_boot_time = bt;
            if (stats.boot_time > 0)
            {
                uint16_t y;
                uint8_t mo, d, h, mi, s;
                unix_to_datetime(stats.boot_time + UTC8_OFFSET_SEC, &y, &mo, &d, &h, &mi, &s);
                lv_label_set_text_fmt(tr_sys_b, "Boot: %04d-%02d-%02d %02d:%02d", (int)y, (int)mo, (int)d, (int)h, (int)mi);
            }
            else
            {
                lv_label_set_text(tr_sys_b, "Boot: N/A");
            }
        }
    }
    if (tr_sys_h)
    {
        if (strcmp(stats.hostname, s_last_hostname) != 0)
        {
            if (strlen(stats.hostname) > 0)
                lv_label_set_text_fmt(tr_sys_h, "Host: %s", stats.hostname);
            else
                lv_label_set_text(tr_sys_h, "Host: N/A");
            strncpy(s_last_hostname, stats.hostname, sizeof(s_last_hostname) - 1);
        }
    }
    if (tr_sys_o)
    {
        if (strcmp(stats.os_platform, s_last_os_platform) != 0)
        {
            if (strlen(stats.os_platform) > 0)
                lv_label_set_text_fmt(tr_sys_o, "OS: %s", stats.os_platform);
            else
                lv_label_set_text(tr_sys_o, "OS: N/A");
            strncpy(s_last_os_platform, stats.os_platform, sizeof(s_last_os_platform) - 1);
        }
    }

    /* ---- ENV ---- */
    if (tr_env_t && stats.sht3x_valid)
    {
        if (stats.sht3x_temperature != s_last_env_temp)
        {
            lv_label_set_text_fmt(tr_env_t, "Temp: %.1f\xC2\xB0\x43 / %.1f\xC2\xB0\x46",
                stats.sht3x_temperature, stats.sht3x_temperature_f);
            s_last_env_temp = stats.sht3x_temperature;
        }
    }
    if (tr_env_h && stats.sht3x_valid)
    {
        if (stats.sht3x_humidity != s_last_env_humi)
        {
            lv_label_set_text_fmt(tr_env_h, "Humi: %.1f%%", stats.sht3x_humidity);
            s_last_env_humi = stats.sht3x_humidity;
        }
    }
    /* Threshold guard — evaluate only after each datum has been received at least once */
    if (stats.sht3x_valid) s_env_data_seen = true;
    if (stats.mem > 0) s_ram_data_seen = true;
    if (stats.disk > 0) s_disk_data_seen = true;
    if (stats.battery_percent > 0 || stats.battery_plugged) s_bat_data_seen = true;
    if (stats.gpu_usage > 0 || strlen(stats.gpu_name) > 0) s_gpu_data_seen = true;

    g_env_over = s_env_data_seen && (stats.sht3x_valid && stats.sht3x_temperature > g_flash_threshold.env_temp_c);
    g_ram_over = s_ram_data_seen && (stats.mem > g_flash_threshold.ram_pct);
    g_disk_over = s_disk_data_seen && (stats.disk > g_flash_threshold.disk_pct);
    g_bat_over = s_bat_data_seen && (stats.battery_percent > 0 && !stats.battery_plugged
        && stats.battery_percent < g_flash_threshold.bat_low_pct);
    g_gpu_over = s_gpu_data_seen && (stats.gpu_usage > g_flash_threshold.gpu_pct);

    /* ---- USER ---- */
    if (tr_user && strlen(stats.current_user) > 0)
    {
        if (strcmp(stats.current_user, s_last_user) != 0)
        {
            lv_label_set_text_fmt(tr_user, " %s", stats.current_user);
            strncpy(s_last_user, stats.current_user, sizeof(s_last_user) - 1);
        }
    }

    /* ---- CLOCK ---- */
    update_clock_v3(tr_time);
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
        int x = 4 + (rand() % (cx * 2 - 8));
        int y = 4 + (rand() % (cy * 2 - 8));
        int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy < r * r) { *ox = x; *oy = y; return true; }
    }
    return false;
}

/** Init particle system + precompute circle bounds cache */
static void init_particles(void)
{
    int32_t const cx = 70, cy = 70, r = 60;
    for (int i = 0; i < PARTICLE_COUNT; i++)
    {
        int x, y;
        rand_pt_in_circle(cx, cy, r, &x, &y);
        s_particles[i].x_base = (int16_t)x;
        s_particles[i].y_base = (int16_t)y;
        s_particles[i].phase = (uint8_t)(rand() % 256);
        s_particles[i].speed = (uint8_t)(1 + (rand() % 3));
        s_particles[i].size = (uint8_t)(1 + (rand() % 2));
        s_particles[i].alpha = (uint8_t)(180 + rand() % 76);
    }

    /* Pre-compute circle half-width cache for CPU canvas (cx=70, cy=70, r=68) */
    if (!s_circle_cached)
    {
        for (int y = 0; y < CIRCLE_H; y++)
        {
            int dy = y - CIRCLE_CY;
            if (dy < 0) dy = -dy;
            if (dy >= CIRCLE_R)
                s_circle_half[y] = 0;
            else
                s_circle_half[y] = (int16_t)(lv_sqrt32(
                    (uint32_t)(CIRCLE_R * CIRCLE_R - dy * dy)) + 1);
        }
        s_circle_cached = true;
    }
}

/** Draw water body fill (uses precomputed circle bounds cache) */
static void draw_water_body(int wl, lv_color32_t* px, int w, int h,
    int cx, int cy, int r, lv_color_t col)
{
    (void)cx; (void)cy; (void)r;
    for (int y = wl; y < h; y++)
    {
        int half = s_circle_half[y];
        if (half <= 0) continue;
        int x0 = cx - half, x1 = cx + half;
        if (x0 < 0) x0 = 0; if (x1 >= w) x1 = w - 1;
        uint8_t a = (uint8_t)(20 + ((y - wl) * 35 / (h - wl > 0 ? h - wl : 1)));
        if (a > 55) a = 55;
        for (int x = x0; x <= x1; x++)
        {
            uint32_t idx = (uint32_t)(y * w + x);
            px[idx] = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = a };
        }
    }
}

/** Draw water wave line (uses precomputed circle bounds cache) */
static void draw_water_wave(int wl, lv_color32_t* px, int w, int h,
    int cx, int cy, int r, lv_color_t col, int ph)
{
    (void)cx; (void)cy; (void)r;
    if (wl >= h || wl < 5) return;
    int half = s_circle_half[wl];
    if (half <= 0) return;
    int x0 = cx - half, x1 = cx + half;
    if (x0 < 0) x0 = 0; if (x1 >= w) x1 = w - 1;
    for (int x = x0; x <= x1; x++)
    {
        int wo = tri_wave((x * 6 + ph * 2) & 0xFF, 3);
        int wy = wl + wo;
        for (int ly = wy - 1; ly <= wy + 1; ly++)
        {
            if (ly < 0 || ly >= h) continue;
            int h2 = s_circle_half[ly];
            if (h2 <= 0) continue;
            int xl = x - 2, xr = x + 2;
            if (xl < cx - h2) xl = cx - h2; if (xr > cx + h2) xr = cx + h2;
            for (int lx = xl; lx <= xr; lx++)
            {
                if (lx < 0 || lx >= w) continue;
                px[(uint32_t)(ly * w + lx)] = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = 120 };
            }
        }
    }
}

/** Draw rising bubble particles */
static void draw_particles(lv_color32_t* px, int w, int h,
    int cx, int cy, int r, int wl, lv_color_t col)
{
    for (int i = 0; i < PARTICLE_COUNT; i++)
    {
        int dx = s_particles[i].x_base - cx, dy = s_particles[i].y_base - cy;
        if (dx * dx + dy * dy >= r * r) continue;
        if (s_particles[i].y_base < wl) continue;
        if (s_particles[i].x_base < 1 || s_particles[i].x_base >= w - 1 || s_particles[i].y_base < 1 || s_particles[i].y_base >= h - 1) continue;
        uint8_t a = s_particles[i].alpha;
        uint32_t idx = (uint32_t)(s_particles[i].y_base * w + s_particles[i].x_base);
        px[idx] = (lv_color32_t){ .red = col.red, .green = col.green, .blue = col.blue, .alpha = a };
        for (int dy2 = -1; dy2 <= 1; dy2++)
        {
            for (int dx2 = -1; dx2 <= 1; dx2++)
            {
                if (dx2 == 0 && dy2 == 0) continue;
                int nx = s_particles[i].x_base + dx2, ny = s_particles[i].y_base + dy2;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                int d2 = nx - cx, d2y = ny - cy;
                if (d2 * d2 + d2y * d2y >= r * r) continue;
                uint32_t nidx = (uint32_t)(ny * w + nx);
                uint8_t glow = (uint8_t)((uint32_t)a * 35 / 255);
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
        s_particles[i].y_base -= (int16_t)(s_particles[i].speed * 5);
        if (s_particles[i].y_base < -5)
        {
            s_particles[i].y_base = (int16_t)(h + (rand() % 20));
            int x, y;
            rand_pt_in_circle(cx, cy, r, &x, &y);
            s_particles[i].x_base = (int16_t)x;
            s_particles[i].alpha = (uint8_t)(120 + rand() % 100);
        }
    }
}

/** Render CPU circle — restore baseline, draw water/particles/text on top */
static void render_cpu_canvas_particles(int pct)
{
    int32_t const w = 140, h = 140, cx = 70, cy = 70, radius = 68;
    int pctc = (pct < 0) ? 0 : ((pct > 100) ? 100 : pct);
    lv_color_t accent = heat_color((float)pctc);
    lv_draw_buf_t* dbuf = lv_canvas_get_draw_buf(vo_cpu_canvas);
    lv_color32_t* px = (lv_color32_t*)dbuf->data;
    int wl = (h * (100 - pctc)) / 100;

    /* Vertical gradient precalc (for edge seal + first baseline generation) */
    int32_t const cp_y0 = 16, cp_h = 376;
    lv_color32_t grad_row[140];
    for (int yi = 0; yi < h; yi++)
    {
        int t = cp_y0 + yi;
        grad_row[yi].red = (uint8_t)(18 - 10 * t / cp_h);
        grad_row[yi].green = (uint8_t)(18 - 10 * t / cp_h);
        grad_row[yi].blue = (uint8_t)(38 - 14 * t / cp_h);
        grad_row[yi].alpha = 0xFF;
    }

    /* First run: generate baseline cache (bit-accurate full-frame gradient) */
    if (s_canvas_baseline == NULL)
    {
        s_canvas_baseline = (lv_color32_t*)lv_malloc(
            (size_t)(w * h) * sizeof(lv_color32_t));
        if (s_canvas_baseline)
        {
            for (int yi = 0; yi < h; yi++)
            {
                lv_color32_t bg = grad_row[yi];
                uint32_t base = (uint32_t)(yi * w);
                for (uint32_t i = 0; i < (uint32_t)w; i++)
                    s_canvas_baseline[base + i] = bg;
            }
        }
    }

    /* Restore from baseline (memcpy -> corners+circle edge 100% bit-identical) */
    if (s_canvas_baseline)
        memcpy(px, s_canvas_baseline, (size_t)(w * h) * sizeof(lv_color32_t));
    else
    {
        /* fallback — baseline allocation failed */
        for (int yi = 0; yi < h; yi++)
        {
            lv_color32_t bg = grad_row[yi];
            uint32_t base = (uint32_t)(yi * w);
            for (uint32_t i = 0; i < (uint32_t)w; i++)
                px[base + i] = bg;
        }
    }

    /* Water body */
    if (wl < h) draw_water_body(wl, px, w, h, cx, cy, radius, accent);
    /* Water wave */
    if (wl < h && wl > 0) draw_water_wave(wl, px, w, h, cx, cy, radius, accent, s_anim_phase);
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
        if (half <= 0) continue;
        for (int d = 0; d < 3; d++)
        {
            int hw = half - d;
            if (hw < 0) break;
            lv_color32_t col = (d <= 1) ? accent32 : grad_row[y];
            int xl = cx - hw; if (xl >= 0) px[(uint32_t)(y * w + xl)] = col;
            int xr = cx + hw; if (xr < w) px[(uint32_t)(y * w + xr)] = col;
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
        dsc.text = buf;
        dsc.font = &lv_font_montserrat_28;
        dsc.color = lv_color_make(0xFF, 0xFF, 0xFF);
        dsc.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t a = { cx - 36, cy - 16, cx + 36, cy + 16 };
        lv_draw_label(&layer, &dsc, &a);

        /* CPU label */
        lv_draw_label_dsc_init(&dsc);
        dsc.text = "CPU";
        dsc.font = &lv_font_montserrat_16;
        dsc.color = accent;
        dsc.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t a2 = { cx - 18, cy + 24, cx + 18, cy + 42 };
        lv_draw_label(&layer, &dsc, &a2);

        lv_canvas_finish_layer(vo_cpu_canvas, &layer);
    }
    lv_obj_invalidate(vo_cpu_canvas);
}

/** Particle animation timer callback (500ms = 2fps) */
static void cpu_particle_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!vo_cpu_canvas) return;
    s_current_pct += (s_target_pct - s_current_pct) * 0.34f;
    if (s_current_pct < 0.01f && s_target_pct < 0.01f) s_current_pct = 0.0f;
    if (s_current_pct > 99.99f && s_target_pct > 99.99f) s_current_pct = 100.0f;
    s_anim_phase = (s_anim_phase + 15) % 256;
    update_particles(140);
    render_cpu_canvas_particles((int)(s_current_pct + 0.5f));
}

/* ========================================================================
 * V3 update — Layout B / VORTEX
 * ======================================================================== */
void update_layout_vortex(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    if (!stats.has_data)
        return;

    /* ---- CPU ring (Canvas) ---- */
    if (vo_cpu_canvas)
    {
        int val = (int)(stats.cpu + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;
        if (val != s_last_cpu_pct)
        {
            s_target_pct = (float)val;
            s_last_cpu_pct = val;
        }
    }
    if (vo_cpu_freq)
    {
        float freq = stats.cpu_freq_current;
        if (freq != s_last_cpu_freq)
        {
            if (stats.cpu_freq_current > 0)
                lv_label_set_text_fmt(vo_cpu_freq, "Freq: %.0f MHz", freq);
            else if (stats.cpu_freq_max > 0)
                lv_label_set_text_fmt(vo_cpu_freq, "Freq: up to %.0f MHz", stats.cpu_freq_max);
            else
                lv_label_set_text(vo_cpu_freq, "Freq: N/A");
            s_last_cpu_freq = freq;
        }
    }
    if (vo_cpu_temp)
    {
        float temp = stats.cpu_temp;
        if (temp != s_last_cpu_temp)
        {
            if (stats.cpu_temp_valid)
                lv_label_set_text_fmt(vo_cpu_temp, "Temp: %.1f \xC2\xB0\x43", temp);
            else
                lv_label_set_text(vo_cpu_temp, "Temp: N/A");
            s_last_cpu_temp = temp;
        }
    }

    /* Flash alert: CPU or temp over threshold -> fast_flash_tick handles border/shadow */
    {
        s_cpu_data_seen = s_cpu_data_seen || (stats.cpu > 0) || stats.cpu_temp_valid;
        bool over = s_cpu_data_seen && ((stats.cpu > g_flash_threshold.cpu_pct) || (stats.cpu_temp_valid && stats.cpu_temp > g_flash_threshold.cpu_temp_c));
        g_cpu_over = over;
    }

    /* ---- RAM ---- */
    if (vo_ram_val || vo_ram_bar)
    {
        int val = (int)(stats.mem + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_ram_pct)
        {
            lv_color_t c = heat_color(stats.mem);
            if (vo_ram_val)
            {
                lv_label_set_text_fmt(vo_ram_val, "%d%%", val);
                lv_obj_set_style_text_color(vo_ram_val, c, 0);
            }
            if (vo_ram_bar)
            {
                lv_bar_set_value(vo_ram_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(vo_ram_bar, c, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(vo_ram_bar, c, LV_PART_INDICATOR);
            }
            s_last_ram_pct = val;
        }
    }
    if (vo_ram_swap && stats.mem_total > 0)
    {
        if (stats.mem_used != s_last_mem_used || stats.mem_total != s_last_mem_total)
        {
            char used_str[16], total_str[16];
            format_bytes(stats.mem_used, used_str, sizeof(used_str));
            format_bytes(stats.mem_total, total_str, sizeof(total_str));
            lv_label_set_text_fmt(vo_ram_swap, "%s / %s", used_str, total_str);
            s_last_mem_used = stats.mem_used;
            s_last_mem_total = stats.mem_total;
        }
    }
    if (vo_ram_swap2)
    {
        float swap = stats.swap_percent;
        if (swap != s_last_swap_pct)
        {
            if (stats.swap_percent >= 0)
                lv_label_set_text_fmt(vo_ram_swap2, "Swap: %.1f%%", swap);
            else
                lv_label_set_text(vo_ram_swap2, "");
            s_last_swap_pct = swap;
        }
    }

    /* ---- DISK ---- */
    if (vo_dsk_val || vo_dsk_bar)
    {
        int val = (int)(stats.disk + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_dsk_pct)
        {
            lv_color_t c = heat_color(stats.disk);
            if (vo_dsk_val)
            {
                lv_label_set_text_fmt(vo_dsk_val, "%d%%", val);
                lv_obj_set_style_text_color(vo_dsk_val, c, 0);
            }
            if (vo_dsk_bar)
            {
                lv_bar_set_value(vo_dsk_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(vo_dsk_bar, c, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(vo_dsk_bar, c, LV_PART_INDICATOR);
            }
            s_last_dsk_pct = val;
        }
    }
    if (vo_dsk_io)
    {
        float io_val = (stats.disk_io_percent >= 0) ? stats.disk_io_percent : -1.0f;
        if (io_val != s_last_dsk_io)
        {
            if (stats.disk_io_percent >= 0)
                lv_label_set_text_fmt(vo_dsk_io, "IO Util: %.1f%%", stats.disk_io_percent);
            else
                lv_label_set_text(vo_dsk_io, "IO Util: N/A");
            s_last_dsk_io = io_val;
        }
    }

    /* ---- BATT ---- */
    if (vo_bat_val || vo_bat_bar)
    {
        int val = (int)(stats.battery_percent + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;
        if (val != s_last_bat_pct)
        {
            s_last_bat_pct = val;
            lv_color_t bc;
            if (stats.battery_percent > 50)
                bc = lv_color_make(0x44, 0xCC, 0x44);
            else if (stats.battery_percent > 20)
                bc = lv_color_make(0xFF, 0xAA, 0x00);
            else
                bc = lv_color_make(0xFF, 0x33, 0x33);

            if (vo_bat_val)
            {
                if (stats.battery_percent > 0)
                {
                    lv_label_set_text_fmt(vo_bat_val, "%d%%", val);
                    lv_obj_set_style_text_color(vo_bat_val, bc, 0);
                }
                else
                {
                    lv_label_set_text(vo_bat_val, "N/A");
                    lv_obj_set_style_text_color(vo_bat_val, lv_color_make(0x66, 0x66, 0x88), 0);
                }
            }
            if (vo_bat_bar)
            {
                lv_bar_set_value(vo_bat_bar, val, LV_ANIM_ON);
                lv_obj_set_style_bg_color(vo_bat_bar, bc, LV_PART_INDICATOR);
                lv_obj_set_style_shadow_color(vo_bat_bar, bc, LV_PART_INDICATOR);
            }
        }  /* if val != s_last_bat_pct */
    }
    if (vo_bat_sts)
    {
        bool plugged = stats.battery_plugged;
        if (plugged != s_last_bat_plugged)
        {
            if (stats.battery_plugged)
                lv_label_set_text(vo_bat_sts, "Plugged In");
            else if (stats.battery_percent > 0)
                lv_label_set_text(vo_bat_sts, "On Battery");
            else
                lv_label_set_text(vo_bat_sts, "No Battery");
            s_last_bat_plugged = plugged;

            /* Update battery dot color on change - plugged=green, battery=red */
            if (vo_bat_icon)
            {
                if (plugged)
                    lv_obj_set_style_bg_color(vo_bat_icon,
                        lv_color_make(0x44, 0xDD, 0x44), 0);  /* green */
                else
                    lv_obj_set_style_bg_color(vo_bat_icon,
                        lv_color_make(0xFF, 0x33, 0x33), 0);  /* red */
            }
        }
    }

    /* ---- GPU ---- */
    if (vo_gpu_name)
    {
        if (strcmp(stats.gpu_name, s_last_gpu_name) != 0)
        {
            if (strlen(stats.gpu_name) > 0)
                lv_label_set_text(vo_gpu_name, stats.gpu_name);
            else
                lv_label_set_text(vo_gpu_name, "GPU: N/A");
            strncpy(s_last_gpu_name, stats.gpu_name, sizeof(s_last_gpu_name) - 1);
        }
    }
    if (vo_gpu_val || vo_gpu_bar)
    {
        float usage = stats.gpu_usage;
        if (usage != s_last_gpu_usage)
        {
            if (stats.gpu_usage >= 0 && strlen(stats.gpu_name) > 0)
            {
                int gval = (int)(stats.gpu_usage + 0.5f);
                if (gval > 100) gval = 100;
                lv_color_t gc = heat_color(stats.gpu_usage);

                if (vo_gpu_val)
                {
                    lv_label_set_text_fmt(vo_gpu_val, "%d%%", gval);
                    lv_obj_set_style_text_color(vo_gpu_val, gc, 0);
                }
                if (vo_gpu_bar)
                {
                    lv_bar_set_value(vo_gpu_bar, gval, LV_ANIM_ON);
                    lv_obj_set_style_bg_color(vo_gpu_bar, gc, LV_PART_INDICATOR);
                    lv_obj_set_style_shadow_color(vo_gpu_bar, gc, LV_PART_INDICATOR);
                }
            }
            else
            {
                if (vo_gpu_val)
                {
                    lv_label_set_text(vo_gpu_val, "N/A");
                    lv_obj_set_style_text_color(vo_gpu_val, lv_color_make(0x66, 0x66, 0x88), 0);
                }
                if (vo_gpu_bar)
                    lv_bar_set_value(vo_gpu_bar, 0, LV_ANIM_ON);
            }
            s_last_gpu_usage = usage;
        }
    }
    if (vo_gpu_tm)
    {
        bool name_changed = (strcmp(stats.gpu_name, s_last_gpu_name) != 0);
        bool temp_changed = (stats.gpu_temp_c != s_last_gpu_temp);
        bool mem_changed = (stats.gpu_mem_used_mb != s_last_gpu_mem);
        if (name_changed || temp_changed || mem_changed)
        {
            if (strlen(stats.gpu_name) > 0)
            {
                char temp_str[32] = "N/A";
                char mem_str[48] = "N/A";
                if (stats.gpu_temp_c >= 0)
                    snprintf(temp_str, sizeof(temp_str), "%.0f \xC2\xB0\x43", stats.gpu_temp_c);
                if (stats.gpu_mem_used_mb >= 0 && stats.gpu_mem_total_mb >= 0)
                    snprintf(mem_str, sizeof(mem_str), "%.0f/%.0f MB",
                        stats.gpu_mem_used_mb, stats.gpu_mem_total_mb);
                else if (stats.gpu_mem_used_mb >= 0)
                    snprintf(mem_str, sizeof(mem_str), "%.0f MB used", stats.gpu_mem_used_mb);
                lv_label_set_text_fmt(vo_gpu_tm, "%s  %s", temp_str, mem_str);
            }
            else
            {
                lv_label_set_text(vo_gpu_tm, "No GPU data available");
            }
            strncpy(s_last_gpu_name, stats.gpu_name, sizeof(s_last_gpu_name) - 1);
            s_last_gpu_temp = stats.gpu_temp_c;
            s_last_gpu_mem = stats.gpu_mem_used_mb;
        }
    }

    /* ---- NETWORK ---- */
    if (vo_net_tx)
    {
        float tx = stats.net_upload_kbps;
        if (tx != s_last_net_tx)
        {
            lv_label_set_text_fmt(vo_net_tx, "TX: %.1f KB/s", tx);
            s_last_net_tx = tx;
        }
    }
    if (vo_net_rx)
    {
        float rx = stats.net_download_kbps;
        if (rx != s_last_net_rx)
        {
            lv_label_set_text_fmt(vo_net_rx, "RX: %.1f KB/s", rx);
            s_last_net_rx = rx;
        }
    }

    /* ---- SYSTEM ---- */
    if (vo_sys_p)
    {
        uint32_t pc = stats.process_count;
        if (pc != s_last_proc_cnt)
        {
            lv_label_set_text_fmt(vo_sys_p, "Processes: %d", (int)pc);
            s_last_proc_cnt = pc;
        }
    }
    if (vo_sys_c)
    {
        uint8_t cores = stats.cpu_cores_logical;
        if (cores != s_last_cores)
        {
            lv_label_set_text_fmt(vo_sys_c, "Cores: %dP / %dL",
                (int)stats.cpu_cores_physical,
                (int)stats.cpu_cores_logical);
            s_last_cores = cores;
        }
    }
    if (vo_sys_b)
    {
        uint32_t bt = stats.boot_time;
        if (bt != s_last_boot_time)
        {
            if (stats.boot_time > 0)
            {
                uint16_t y;
                uint8_t mo, d, h, mi, s;
                unix_to_datetime(stats.boot_time + UTC8_OFFSET_SEC, &y, &mo, &d, &h, &mi, &s);
                lv_label_set_text_fmt(vo_sys_b, "Boot: %04d-%02d-%02d %02d:%02d", (int)y, (int)mo, (int)d, (int)h, (int)mi);
            }
            else
            {
                lv_label_set_text(vo_sys_b, "Boot: N/A");
            }
            s_last_boot_time = bt;
        }
    }
    if (vo_sys_h)
    {
        if (strcmp(stats.hostname, s_last_hostname) != 0)
        {
            if (strlen(stats.hostname) > 0)
                lv_label_set_text_fmt(vo_sys_h, "Host: %s", stats.hostname);
            else
                lv_label_set_text(vo_sys_h, "Host: N/A");
            strncpy(s_last_hostname, stats.hostname, sizeof(s_last_hostname) - 1);
        }
    }
    if (vo_sys_o)
    {
        if (strcmp(stats.os_platform, s_last_os_platform) != 0)
        {
            if (strlen(stats.os_platform) > 0)
                lv_label_set_text_fmt(vo_sys_o, "OS: %s", stats.os_platform);
            else
                lv_label_set_text(vo_sys_o, "OS: N/A");
            strncpy(s_last_os_platform, stats.os_platform, sizeof(s_last_os_platform) - 1);
        }
    }   /* end of if (vo_cpu_canvas) */

    /* ---- ENV ---- */
    if (vo_env_t && stats.sht3x_valid)
    {
        if (stats.sht3x_temperature != s_last_env_temp)
        {
            lv_label_set_text_fmt(vo_env_t, "Temp: %.1f\xC2\xB0\x43 / %.1f\xC2\xB0\x46",
                stats.sht3x_temperature, stats.sht3x_temperature_f);
            s_last_env_temp = stats.sht3x_temperature;
        }
    }
    if (vo_env_h && stats.sht3x_valid)
    {
        if (stats.sht3x_humidity != s_last_env_humi)
        {
            lv_label_set_text_fmt(vo_env_h, "Humi: %.1f%%", stats.sht3x_humidity);
            s_last_env_humi = stats.sht3x_humidity;
        }
    }
    /* Threshold guard — evaluate only after each datum has been received at least once */
    if (stats.sht3x_valid) s_env_data_seen = true;
    if (stats.mem > 0) s_ram_data_seen = true;
    if (stats.disk > 0) s_disk_data_seen = true;
    if (stats.battery_percent > 0 || stats.battery_plugged) s_bat_data_seen = true;
    if (stats.gpu_usage > 0 || strlen(stats.gpu_name) > 0) s_gpu_data_seen = true;

    g_env_over = s_env_data_seen && stats.sht3x_valid && (stats.sht3x_temperature > g_flash_threshold.env_temp_c);
    g_ram_over = s_ram_data_seen && (stats.mem > g_flash_threshold.ram_pct);
    g_disk_over = s_disk_data_seen && (stats.disk > g_flash_threshold.disk_pct);
    g_bat_over = s_bat_data_seen && (stats.battery_percent > 0 && !stats.battery_plugged
        && stats.battery_percent < g_flash_threshold.bat_low_pct);
    g_gpu_over = s_gpu_data_seen && (stats.gpu_usage > g_flash_threshold.gpu_pct);

    /* ---- USER ---- */
    if (vo_user && strlen(stats.current_user) > 0)
    {
        if (strcmp(stats.current_user, s_last_user) != 0)
        {
            lv_label_set_text_fmt(vo_user, " %s", stats.current_user);
            strncpy(s_last_user, stats.current_user, sizeof(s_last_user) - 1);
        }
    }

    /* ---- CLOCK ---- */
    update_clock_v3(vo_time);
}

/* ========================================================================
 * V3 update — Layout C / PULSE
 * ======================================================================== */
void update_layout_pulse(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    if (!stats.has_data)
        return;

    /* ---- CPU box ---- */
    if (pu_cpu_val)
    {
        int val = (int)(stats.cpu + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_cpu_pct)
        {
            lv_color_t c = heat_color(stats.cpu);
            lv_label_set_text_fmt(pu_cpu_val, "%d%%", val);
            lv_obj_set_style_text_color(pu_cpu_val, c, 0);
            s_last_cpu_pct = val;
        }
    }
    if (pu_cpu_sub)
    {
        char sub_buf[32] = "";
        if (stats.cpu_freq_current > 0)
            snprintf(sub_buf, sizeof(sub_buf), "Freq: %.0f MHz", stats.cpu_freq_current);
        else if (stats.cpu_freq_max > 0)
            snprintf(sub_buf, sizeof(sub_buf), "Freq: up to %.0f MHz", stats.cpu_freq_max);
        else
            snprintf(sub_buf, sizeof(sub_buf), "Freq: N/A");

        if (s_last_cpu_freq != stats.cpu_freq_current || s_last_cpu_pct == DIFF_INIT_INT)
        {
            lv_label_set_text(pu_cpu_sub, sub_buf);
            s_last_cpu_freq = stats.cpu_freq_current;
        }
    }
    if (pu_cpu_temp)
    {
        if (stats.cpu_temp_valid)
        {
            if (s_last_cpu_temp != stats.cpu_temp)
            {
                lv_label_set_text_fmt(pu_cpu_temp, "Temp: %.1f \xC2\xB0\x43", stats.cpu_temp);
                s_last_cpu_temp = stats.cpu_temp;
            }
        }
        else
        {
            if (s_last_cpu_temp != -1.0f)
            {
                lv_label_set_text(pu_cpu_temp, "Temp: N/A");
                s_last_cpu_temp = -1.0f;
            }
        }
    }

    /* Flash alert: CPU >80% or temp >70C -> blink value+bar (fast flash handles card) */
    {
        s_cpu_data_seen = s_cpu_data_seen || (stats.cpu > 0) || stats.cpu_temp_valid;
        bool over = s_cpu_data_seen && ((stats.cpu > g_flash_threshold.cpu_pct) || (stats.cpu_temp_valid && stats.cpu_temp > g_flash_threshold.cpu_temp_c));
        g_cpu_over = over;
        lv_opa_t opa = (over && !g_flash_on) ? LV_OPA_40 : LV_OPA_COVER;
        if (pu_cpu_val) lv_obj_set_style_text_opa(pu_cpu_val, opa, 0);
    }

    /* ---- RAM box ---- */
    if (pu_ram_val)
    {
        int val = (int)(stats.mem + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_ram_pct)
        {
            lv_color_t c = heat_color(stats.mem);
            lv_label_set_text_fmt(pu_ram_val, "%d%%", val);
            lv_obj_set_style_text_color(pu_ram_val, c, 0);
            s_last_ram_pct = val;
        }
    }
    if (pu_ram_sub && stats.mem_total > 0)
    {
        if (stats.mem_used != s_last_mem_used || stats.mem_total != s_last_mem_total)
        {
            char used_str[16], total_str[16];
            format_bytes(stats.mem_used, used_str, sizeof(used_str));
            format_bytes(stats.mem_total, total_str, sizeof(total_str));
            lv_label_set_text_fmt(pu_ram_sub, "%s / %s", used_str, total_str);
            s_last_mem_used = stats.mem_used;
            s_last_mem_total = stats.mem_total;
        }
    }
    if (pu_ram_swap2)
    {
        float swap = stats.swap_percent;
        if (swap != s_last_swap_pct)
        {
            if (stats.swap_percent >= 0)
                lv_label_set_text_fmt(pu_ram_swap2, "Swap: %.1f%%", swap);
            else
                lv_label_set_text(pu_ram_swap2, "");
            s_last_swap_pct = swap;
        }
    }

    /* ---- DISK box ---- */
    if (pu_dsk_val)
    {
        int val = (int)(stats.disk + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_dsk_pct)
        {
            lv_color_t c = heat_color(stats.disk);
            lv_label_set_text_fmt(pu_dsk_val, "%d%%", val);
            lv_obj_set_style_text_color(pu_dsk_val, c, 0);
            s_last_dsk_pct = val;
        }
    }
    if (pu_dsk_sub)
    {
        float io_val = (stats.disk_io_percent >= 0) ? stats.disk_io_percent : -1.0f;
        if (io_val != s_last_dsk_io)
        {
            if (stats.disk_io_percent >= 0)
                lv_label_set_text_fmt(pu_dsk_sub, "IO Util: %.1f%%", stats.disk_io_percent);
            else
                lv_label_set_text(pu_dsk_sub, "IO Util: N/A");
            s_last_dsk_io = io_val;
        }
    }

    /* ---- BATT box ---- */
    if (pu_bat_val)
    {
        int val = (int)(stats.battery_percent + 0.5f);
        if (val > 100) val = 100;
        if (val < 0) val = 0;

        if (val != s_last_bat_pct)
        {
            lv_color_t bc;
            if (stats.battery_percent > 50)
                bc = lv_color_make(0x44, 0xCC, 0x44);
            else if (stats.battery_percent > 20)
                bc = lv_color_make(0xFF, 0xAA, 0x00);
            else
                bc = lv_color_make(0xFF, 0x33, 0x33);

            if (stats.battery_percent > 0)
            {
                lv_label_set_text_fmt(pu_bat_val, "%d%%", val);
                lv_obj_set_style_text_color(pu_bat_val, bc, 0);
            }
            else
            {
                lv_label_set_text(pu_bat_val, "N/A");
                lv_obj_set_style_text_color(pu_bat_val, lv_color_make(0x66, 0x66, 0x88), 0);
            }
            s_last_bat_pct = val;
        }
    }
    if (pu_bat_sub)
    {
        bool plugged = stats.battery_plugged;
        if (plugged != s_last_bat_plugged)
        {
            if (stats.battery_plugged)
                lv_label_set_text(pu_bat_sub, "Plugged In");
            else if (stats.battery_percent > 0)
                lv_label_set_text(pu_bat_sub, "On Battery");
            else
                lv_label_set_text(pu_bat_sub, "No Battery");
            s_last_bat_plugged = plugged;
        }
    }

    /* ---- GPU box ---- */
    if (pu_gpu_val)
    {
        float usage = stats.gpu_usage;
        if (usage != s_last_gpu_usage)
        {
            if (usage >= 0 && strlen(stats.gpu_name) > 0)
            {
                int gval = (int)(usage + 0.5f);
                if (gval > 100) gval = 100;
                lv_color_t gc = heat_color(usage);
                lv_label_set_text_fmt(pu_gpu_val, "%d%%", gval);
                lv_obj_set_style_text_color(pu_gpu_val, gc, 0);
            }
            else
            {
                lv_label_set_text(pu_gpu_val, "N/A");
                lv_obj_set_style_text_color(pu_gpu_val, lv_color_make(0x66, 0x66, 0x88), 0);
            }
            s_last_gpu_usage = usage;
        }
    }
    if (pu_gpu_sub)
    {
        bool name_changed = (strcmp(stats.gpu_name, s_last_gpu_name) != 0);
        bool temp_changed = (stats.gpu_temp_c != s_last_gpu_temp);
        bool mem_changed = (stats.gpu_mem_used_mb != s_last_gpu_mem);

        if (name_changed || temp_changed || mem_changed)
        {
            if (strlen(stats.gpu_name) > 0)
            {
                char sub_buf[64] = "";
                if (stats.gpu_temp_c >= 0)
                    snprintf(sub_buf, sizeof(sub_buf), "%.0f \xC2\xB0\x43", stats.gpu_temp_c);
                if (stats.gpu_mem_used_mb >= 0)
                    snprintf(sub_buf + strlen(sub_buf), sizeof(sub_buf) - strlen(sub_buf),
                        "  %.0f MB", stats.gpu_mem_used_mb);
                lv_label_set_text(pu_gpu_sub, sub_buf);
            }
            else
            {
                lv_label_set_text(pu_gpu_sub, "GPU N/A");
            }
            strncpy(s_last_gpu_name, stats.gpu_name, sizeof(s_last_gpu_name) - 1);
            s_last_gpu_temp = stats.gpu_temp_c;
            s_last_gpu_mem = stats.gpu_mem_used_mb;
        }
    }

    /* ---- NET box ---- */
    if (pu_net_sub)
    {
        float tx = stats.net_upload_kbps;
        float rx = stats.net_download_kbps;
        if (tx != s_last_net_tx || rx != s_last_net_rx)
        {
            char net_buf[64];
            snprintf(net_buf, sizeof(net_buf),
                "TX: %.1f KB/s  RX: %.1f KB/s", tx, rx);
            lv_label_set_text(pu_net_sub, net_buf);
            s_last_net_tx = tx;
            s_last_net_rx = rx;
        }
    }

    /* ---- SYSTEM info bar ---- */
    if (pu_sys_p)
    {
        uint32_t pc = stats.process_count;
        if (pc != s_last_proc_cnt)
        {
            lv_label_set_text_fmt(pu_sys_p, "Procs: %d", (int)pc);
            s_last_proc_cnt = pc;
        }
    }
    if (pu_sys_c)
    {
        uint8_t cores = stats.cpu_cores_logical;
        if (cores != s_last_cores)
        {
            lv_label_set_text_fmt(pu_sys_c, "Cores: %dP / %dL",
                (int)stats.cpu_cores_physical,
                (int)stats.cpu_cores_logical);
            s_last_cores = cores;
        }
    }
    if (pu_sys_b)
    {
        uint32_t bt = stats.boot_time;
        if (bt != s_last_boot_time)
        {
            if (stats.boot_time > 0)
            {
                uint16_t y;
                uint8_t mo, d, h, mi, s;
                unix_to_datetime(stats.boot_time + UTC8_OFFSET_SEC, &y, &mo, &d, &h, &mi, &s);
                lv_label_set_text_fmt(pu_sys_b, "Boot: %04d-%02d-%02d %02d:%02d", (int)y, (int)mo, (int)d, (int)h, (int)mi);
            }
            else
            {
                lv_label_set_text(pu_sys_b, "Boot: --");
            }
            s_last_boot_time = bt;
        }
    }
    if (pu_sys_o)
    {
        if (strcmp(stats.os_platform, s_last_os_platform) != 0)
        {
            if (strlen(stats.os_platform) > 0)
                lv_label_set_text_fmt(pu_sys_o, "OS: %s", stats.os_platform);
            else
                lv_label_set_text(pu_sys_o, "OS: --");
            strncpy(s_last_os_platform, stats.os_platform, sizeof(s_last_os_platform) - 1);
        }
    }

    /* ---- ENV ---- */
    if (pu_env_t && stats.sht3x_valid)
    {
        if (stats.sht3x_temperature != s_last_env_temp)
        {
            lv_label_set_text_fmt(pu_env_t, "Temp: %.1f\xC2\xB0\x43 / %.1f\xC2\xB0\x46",
                stats.sht3x_temperature, stats.sht3x_temperature_f);
            s_last_env_temp = stats.sht3x_temperature;
        }
    }
    if (pu_env_h && stats.sht3x_valid)
    {
        if (stats.sht3x_humidity != s_last_env_humi)
        {
            lv_label_set_text_fmt(pu_env_h, "Humi: %.1f%%", stats.sht3x_humidity);
            s_last_env_humi = stats.sht3x_humidity;
        }
    }
    /* Threshold guard — evaluate only after each datum has been received at least once */
    if (stats.sht3x_valid) s_env_data_seen = true;
    if (stats.mem > 0) s_ram_data_seen = true;
    if (stats.disk > 0) s_disk_data_seen = true;
    if (stats.battery_percent > 0 || stats.battery_plugged) s_bat_data_seen = true;
    if (stats.gpu_usage > 0 || strlen(stats.gpu_name) > 0) s_gpu_data_seen = true;

    g_env_over = s_env_data_seen && (stats.sht3x_valid && stats.sht3x_temperature > g_flash_threshold.env_temp_c);
    g_ram_over = s_ram_data_seen && (stats.mem > g_flash_threshold.ram_pct);
    g_disk_over = s_disk_data_seen && (stats.disk > g_flash_threshold.disk_pct);
    g_bat_over = s_bat_data_seen && (stats.battery_percent > 0 && !stats.battery_plugged
        && stats.battery_percent < g_flash_threshold.bat_low_pct);
    g_gpu_over = s_gpu_data_seen && (stats.gpu_usage > g_flash_threshold.gpu_pct);

    /* ---- USER ---- */
    if (pu_user && strlen(stats.current_user) > 0)
    {
        if (strcmp(stats.current_user, s_last_user) != 0)
        {
            lv_label_set_text_fmt(pu_user, " %s", stats.current_user);
            strncpy(s_last_user, stats.current_user, sizeof(s_last_user) - 1);
        }
    }

    /* ---- CLOCK ---- */
    update_clock_v3(pu_time);
}

/* ========================================================================
 * V3 update dispatch — called by update_dashboard_ui()
 * ======================================================================== */
void update_current_layout(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    if (!stats.has_data)
        return;

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
        strcmp(stats.gpu_name, s_prev.gpu_name) != 0;

    if (!changed)
        return;

    s_first = false;
    s_prev = stats;

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
