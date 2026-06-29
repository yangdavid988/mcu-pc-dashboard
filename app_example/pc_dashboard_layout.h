#ifndef PC_DASHBOARD_LAYOUT_H
#define PC_DASHBOARD_LAYOUT_H

#include "lvgl.h"
#include "pc_dashboard_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Layout constants */
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH  800
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 480
#endif
#define LAYOUT_COL_GAP   6
#define LAYOUT_CARD_GAP  4

/* Icon creation helper */
lv_obj_t *create_icon_img(lv_obj_t *parent, const lv_img_dsc_t *icon,
                          lv_color_t color, int x, int y);

/* Card / bar / gradient helpers */
lv_obj_t *create_card(lv_obj_t *parent, int w, int h,
                      lv_color_t accent, int y_pos);
lv_obj_t *create_glow_bar(lv_obj_t *parent, int w, int h,
                          lv_color_t track, lv_color_t indicator);
void set_gradient_bg(lv_obj_t *obj, lv_color_t top, lv_color_t bottom);

/* Layout creation functions */
void create_layout_triad(lv_obj_t *parent);
void create_layout_vortex(lv_obj_t *parent);
void create_layout_pulse(lv_obj_t *parent);

/* Layout management */
void destroy_current_layout(void);
void set_layout_container(lv_obj_t *cont);
lv_obj_t *layout_get_container(void);

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

/* Check if layout has been created */
bool layout_is_created(void);

/* Notify layout system that a switch just happened (resets diff tracking, flash state) */
void notify_layout_switched(void);

/* Reset all s_last_* diff tracking to sentinel values (forces full refresh) */
void reset_diff_tracking(void);

/* Flash state for threshold alert (toggled by timer callback) */
extern bool g_flash_on;
void toggle_flash_state(void);

/* Fast flash tick for threshold alert (~150ms, independent of 1Hz data timer) */
void fast_flash_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_DASHBOARD_LAYOUT_H */
