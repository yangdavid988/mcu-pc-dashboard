#ifndef PC_DASHBOARD_LAYOUT_H
#define PC_DASHBOARD_LAYOUT_H

#include "lvgl.h"
#include "ui/pc_dashboard_theme.h"
#include "core/weather.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* CPU circle particle system */
#define PARTICLE_COUNT 48
/* CPU circle precomputed boundary cache (avoids ~600 lv_sqrt32() calls per frame) */
#define CIRCLE_W  140
#define CIRCLE_H  140
#define CIRCLE_CX 70
#define CIRCLE_CY 70
#define CIRCLE_R  68

typedef struct
{
    int16_t x_base;
    int16_t y_base;
    uint8_t phase;
    uint8_t speed;
    uint8_t size;
    uint8_t alpha;
} particle_t;

extern particle_t  s_particles[PARTICLE_COUNT];
extern float       s_current_pct;
extern float       s_target_pct;
extern int32_t     s_anim_phase;
extern lv_timer_t* s_particle_timer;
extern int16_t     s_circle_half[140];
extern bool        s_circle_cached;

/* Per-layout widget pointer externs (defined in base) */
/* Layout A -- TRIAD */
extern lv_obj_t *tr_cpu_bar, *tr_cpu_val, *tr_cpu_freq, *tr_cpu_temp;
extern lv_obj_t *tr_ram_bar, *tr_ram_val, *tr_ram_swap, *tr_ram_swap2;
extern lv_obj_t *tr_dsk_bar, *tr_dsk_val, *tr_dsk_io;
extern lv_obj_t *tr_bat_bar, *tr_bat_val, *tr_bat_sts;
extern lv_obj_t *tr_gpu_bar, *tr_gpu_val, *tr_gpu_name, *tr_gpu_tm;
extern lv_obj_t *tr_io_read, *tr_io_write;
extern lv_obj_t *tr_net_tx, *tr_net_rx;
extern lv_obj_t *tr_sys_p, *tr_sys_c, *tr_sys_b, *tr_sys_h, *tr_sys_o;
extern lv_obj_t *tr_env_t, *tr_env_h;
extern lv_obj_t* tr_weather_info;
extern lv_obj_t* tr_weather_icon;
extern lv_obj_t* tr_weather_main;
extern lv_obj_t* tr_weather_city;
extern lv_obj_t *tr_time, *tr_user, *tr_bat_icon;
extern lv_obj_t *tr_warn_lbl, *tr_warn_icon;

/* Layout B -- VORTEX */
extern lv_obj_t *vo_cpu_freq, *vo_cpu_temp;
extern lv_obj_t *vo_ram_bar, *vo_ram_val, *vo_ram_swap, *vo_ram_swap2;
extern lv_obj_t *vo_dsk_bar, *vo_dsk_val, *vo_dsk_io;
extern lv_obj_t *vo_bat_bar, *vo_bat_val, *vo_bat_sts, *vo_bat_icon;
extern lv_obj_t *vo_gpu_bar, *vo_gpu_val, *vo_gpu_name, *vo_gpu_tm;
extern lv_obj_t *vo_net_tx, *vo_net_rx;
extern lv_obj_t *vo_sys_p, *vo_sys_c, *vo_sys_b, *vo_sys_h, *vo_sys_o;
extern lv_obj_t *vo_env_t, *vo_env_h;
extern lv_obj_t* vo_weather_info;
extern lv_obj_t* vo_weather_icon;
extern lv_obj_t* vo_weather_main;
extern lv_obj_t* vo_weather_city;
extern lv_obj_t *vo_time, *vo_user;
extern lv_obj_t *vo_warn_lbl, *vo_warn_icon;
extern lv_obj_t* vo_cpu_canvas;
extern lv_draw_buf_t* s_cpu_canvas_draw_buf;
extern lv_color32_t* s_canvas_baseline;

/* Layout C -- PULSE */
extern lv_obj_t *pu_cpu_val, *pu_cpu_sub, *pu_cpu_temp;
extern lv_obj_t *pu_ram_val, *pu_ram_sub, *pu_ram_swap2;
extern lv_obj_t *pu_dsk_val, *pu_dsk_sub;
extern lv_obj_t *pu_bat_val, *pu_bat_sub;
extern lv_obj_t *pu_gpu_val, *pu_gpu_sub;
extern lv_obj_t* pu_net_sub;
extern lv_obj_t *pu_sys_p, *pu_sys_c, *pu_sys_b, *pu_sys_o;
extern lv_obj_t *pu_env_t, *pu_env_h;
extern lv_obj_t* pu_weather_info;
extern lv_obj_t* pu_weather_icon;
extern lv_obj_t* pu_weather_main;
extern lv_obj_t* pu_weather_city;
extern lv_obj_t *pu_time, *pu_user;
extern lv_obj_t *pu_warn_lbl, *pu_warn_icon;

/* Shared globals */
extern bool g_cpu_over, g_env_over, g_ram_over, g_disk_over, g_bat_over, g_gpu_over;
extern bool g_reset_flash_prev;
extern uint32_t v3_last_sec;

/* Per-category data-received flags */
extern bool s_cpu_data_seen, s_env_data_seen, s_ram_data_seen;
extern bool s_disk_data_seen, s_bat_data_seen, s_gpu_data_seen;

/* JSON diff tracking — last displayed values (shared across all 3 layouts) */
extern int   s_last_cpu_pct;
extern float s_last_cpu_freq, s_last_cpu_temp;
extern int   s_last_ram_pct;
extern uint64_t s_last_mem_used, s_last_mem_total;
extern float s_last_swap_pct;
extern int   s_last_dsk_pct;
extern float s_last_dsk_io;
extern uint64_t s_last_io_read, s_last_io_write;
extern int   s_last_bat_pct, s_last_bat_plugged;
extern float s_last_gpu_usage, s_last_gpu_temp, s_last_gpu_mem;
extern char  s_last_gpu_name[64];
extern float s_last_net_tx, s_last_net_rx;
extern uint32_t s_last_proc_cnt;
extern uint8_t  s_last_cores;
extern uint32_t s_last_boot_time;
extern char  s_last_hostname[64], s_last_os_platform[64];
extern float s_last_env_temp, s_last_env_humi;
extern char  s_last_weather_city[];
extern char  s_last_weather_main[];
extern float s_last_weather_temp;
extern int   s_last_weather_humi;
extern char  s_last_user[32];
extern bool  s_first;

/* Particle system init (used by vortex layout create via extern) */
void init_particles(void);
void cpu_particle_timer_cb(lv_timer_t* timer);

/* Layout constants */
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 800
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 480
#endif
#define LAYOUT_COL_GAP  6
#define LAYOUT_CARD_GAP 4

/* Diff sentinel values (used across all per-layout update functions) */
#define DIFF_INIT_INT (-999)
#define DIFF_INIT_FLT (-9999.0f)

/* TRIAD panel / card layout (also referenced by VORTEX and PULSE for shared metrics) */
#define TRIAD_PANEL_HEIGHT    376
#define TRIAD_CARD_GAP        4
#define TRIAD_CARD_TOP_OFFSET 4
#define TRIAD_CARD_LEFT_CH    89
#define TRIAD_CARD_MID_GAP    4
#define TRIAD_RIGHT_NET_CH    130
#define TRIAD_CARD_PADDING_H  6
#define TRIAD_CARD_BAR_MARGIN 10
#define TRIAD_CARD_BAR_H      14

    /* Icon creation helper */
    lv_obj_t* create_icon_img(lv_obj_t* parent, const lv_img_dsc_t* icon, lv_color_t color, int x, int y);

    /* Card / bar / gradient helpers */
    lv_obj_t* create_card(lv_obj_t* parent, int w, int h, lv_color_t accent, int y_pos);
    lv_obj_t* create_glow_bar(lv_obj_t* parent, int w, int h, lv_color_t track, lv_color_t indicator);
    void      set_gradient_bg(lv_obj_t* obj, lv_color_t top, lv_color_t bottom);

    /* Layout creation functions */
    void create_layout_triad(lv_obj_t* parent);
    void create_layout_vortex(lv_obj_t* parent);
    void create_layout_pulse(lv_obj_t* parent);

    /* Layout management */
    void      destroy_current_layout(void);
    void      set_layout_container(lv_obj_t* cont);
    lv_obj_t* layout_get_container(void);

    /* Color interpolation */
    lv_color_t heat_color(float percent);
    lv_color_t temp_color(float celsius);

    /* V3 update — refresh current layout widgets with live data */
    void update_current_layout(void);

    /* Per-layout V3 update helpers (dispatched by update_current_layout) */
    void update_layout_triad(void);
    void update_layout_vortex(void);
    void update_layout_pulse(void);

    /* V3 clock update — refresh clock for current layout (called from timer) */
    void update_layout_clock(void);

    /* V3 clock helper — update a single clock label (called from per-layout updates) */
    void update_clock_v3(lv_obj_t* time_label);

    /* Weather UI update — refresh weather display (called from timer, independent of MQTT) */
    void update_weather_ui(void);

    /* Check if layout has been created */
    bool layout_is_created(void);

    /* Notify layout system that a switch just happened (resets diff tracking, flash state) */
    void notify_layout_switched(void);

    /* Reset all s_last_* diff tracking to sentinel values (forces full refresh) */
    void reset_diff_tracking(void);

    /* Flash state for threshold alert (toggled by timer callback) */
    extern bool g_flash_on;
    void        toggle_flash_state(void);

    /* Fast flash tick for threshold alert (~150ms, independent of 1Hz data timer) */
    void fast_flash_tick(void);

    /* Sedentary reminder — corner flash management */
    void sedentary_flash_tick(void);
    void sedentary_flash_create(void);
    void sedentary_flash_destroy(void);
    void sedentary_flash_raise(void);
    void sedentary_flash_recolor(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_DASHBOARD_LAYOUT_H */
