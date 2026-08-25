#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/pc_dashboard_layout.h"
#include "ui/pc_dashboard_theme.h"
#include "config/threshold_config.h"
#include "core/weather.h"
#include "assets/icons/icons.h"
#include "ui/pc_dashboard_ui.h"
#include "log.h"

#ifndef TAG
#define TAG "V3_LAYOUT"
#endif

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
    const theme_t* th    = &g_themes[g_theme_id];
    const char*    tname = theme_get_name(g_theme_id);
    const char*    lname = layout_get_name(LAYOUT_PULSE);

    /* Box / bar dimensions */
#define PULSE_BOX_W   260
#define PULSE_BOW_GAP 6
#define PULSE_ROW1_H  168
#define PULSE_ROW2_H  156
#define PULSE_SYS_H   36

    /* Box x positions (3 boxes, 260w + 6gap, centred in 792-wide mc) */
#define PULSE_BOX0_X 0
#define PULSE_BOX1_X 266 /* 0 + 260 + 6 */
#define PULSE_BOX2_X 530 /* 266 + 260 + 6 */

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
        snprintf(title_buf, sizeof(title_buf), "PC DASHBOARD - %s - %s", lname, tname);
        lv_obj_t* title_lbl = lv_label_create(header);
        lv_label_set_text(title_lbl, title_buf);
        lv_obj_set_style_text_color(title_lbl, th->header, 0);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 52, 0);
    }

    /* User name label */
    create_icon_img(header, &icon_user, lv_color_make(0x88, 0xAA, 0xCC), 430, 2);
    lv_obj_t* user_lbl = lv_label_create(header);
    pu_user            = user_lbl;
    lv_label_set_text(user_lbl, "");
    lv_obj_set_style_text_color(user_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(user_lbl, 460, 4);

    /* Clock label */
    lv_obj_t* clock_lbl = lv_label_create(header);
    pu_time             = clock_lbl;
    lv_label_set_text(clock_lbl, "--:--:--");
    lv_obj_set_style_text_color(clock_lbl, lv_color_make(0x00, 0xFF, 0x88), 0);
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -2, 0);

    /* NO DATA warning */
    {
        lv_obj_t* warn_icon = create_icon_img(header, &icon_warning, th->warn, 680, 2);
        pu_warn_icon        = warn_icon;
        lv_obj_add_flag(warn_icon, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* warn_lbl = lv_label_create(header);
        pu_warn_lbl        = warn_lbl;
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
            lv_obj_t*  box    = lv_obj_create(mc);
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
            pu_cpu_val    = pct;
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
            pu_cpu_sub    = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);

            lv_obj_t* temp = lv_label_create(box);
            pu_cpu_temp    = temp;
            lv_label_set_text(temp, "");
            lv_obj_set_style_text_color(temp, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);
            lv_obj_align(temp, LV_ALIGN_CENTER, 0, 55);
        }

        /* ---- RAM box ---- */
        {
            lv_color_t accent = th->ram;
            lv_obj_t*  box    = lv_obj_create(mc);
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
            pu_ram_val    = pct;
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
            pu_ram_sub    = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);

            lv_obj_t* swap2 = lv_label_create(box);
            pu_ram_swap2    = swap2;
            lv_label_set_text(swap2, "");
            lv_obj_set_style_text_color(swap2, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap2, &lv_font_montserrat_14, 0);
            lv_obj_align(swap2, LV_ALIGN_CENTER, 0, 55);
        }

        /* ---- DISK box ---- */
        {
            lv_color_t accent = th->disk;
            lv_obj_t*  box    = lv_obj_create(mc);
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
            pu_dsk_val    = pct;
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
            pu_dsk_sub    = sub;
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
            pu_sys_p     = pd;
            lv_label_set_text(pd, "Procs: --");
            lv_obj_set_style_text_color(pd, sys_val, 0);
            lv_obj_set_style_text_font(pd, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(pd, 50, 13);
        }

        /* Cores */
        {
            create_icon_img(sys_bar, &icon_cpu, sys_col, 210, 2);
            lv_obj_t* cd = lv_label_create(sys_bar);
            pu_sys_c     = cd;
            lv_label_set_text(cd, "Cores: --");
            lv_obj_set_style_text_color(cd, sys_val, 0);
            lv_obj_set_style_text_font(cd, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(cd, 250, 13);
        }

        /* Boot time — longest, most space */
        {
            create_icon_img(sys_bar, &icon_power_off, sys_col, 410, 2);
            lv_obj_t* bd = lv_label_create(sys_bar);
            pu_sys_b     = bd;
            lv_label_set_text(bd, "Boot: --");
            lv_obj_set_style_text_color(bd, sys_val, 0);
            lv_obj_set_style_text_font(bd, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(bd, 448, 13);
        }

        /* OS */
        {
            create_icon_img(sys_bar, &icon_globe, sys_col, 630, 2);
            lv_obj_t* od = lv_label_create(sys_bar);
            pu_sys_o     = od;
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
            lv_obj_t*  box    = lv_obj_create(mc);
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
            pu_bat_val    = pct;
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
            pu_bat_sub    = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }

        /* ---- GPU box ---- */
        {
            lv_color_t accent = th->gpu;
            lv_obj_t*  box    = lv_obj_create(mc);
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
            pu_gpu_val    = pct;
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
            pu_gpu_sub    = sub;
            lv_label_set_text(sub, "");
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }

        /* ---- NET box ---- */
        {
            lv_color_t accent = th->net;
            lv_obj_t*  box    = lv_obj_create(mc);
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
            pu_net_sub    = sub;
            lv_obj_set_style_text_color(sub, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
            lv_obj_align(sub, LV_ALIGN_CENTER, 0, 39);
        }
    }

    /* ==============================================================
     * 6. Env bar — indoor (SHT3X) + outdoor (weather)
     *    Two-zone layout: Indoor left | Outdoor right | City name
     * ============================================================== */
    {
        int bar_w = 792;
        int bar_h = 32;
        int bx    = 4;
        int by    = 420;

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

        /* Temperature icon — shared env indicator */
        create_icon_img(bar, &icon_temp, th->env, 12, 0);

        /* --- Indoor (SHT3X): temp icon + "28.5°C/82°F  60%" --- */
        lv_obj_t* temp_lbl = lv_label_create(bar);
        pu_env_t           = temp_lbl;
        lv_label_set_text(temp_lbl, "--.-\xC2\xB0\x43 / ---\xC2\xB0\x46  --%");
        lv_obj_set_style_text_color(temp_lbl, lv_color_make(0xAA, 0xFF, 0xCC), 0);
        lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_22, 0);
        lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 56, 0);
        /* pu_env_h intentionally left NULL — humidity folded into pu_env_t */

        /* --- Outdoor weather: icon + "Clear" + "28°C / 82°F  65%" --- */
        pu_weather_icon  = create_icon_img(bar, &icon_sun, lv_color_make(0x88, 0xCC, 0xFF), 305, 0);
        lv_obj_t* w_main = lv_label_create(bar);
        pu_weather_main  = w_main;
        lv_label_set_text(w_main, "");
        lv_obj_set_style_text_color(w_main, lv_color_make(0x88, 0xCC, 0xFF), 0);
        lv_obj_set_style_text_font(w_main, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(w_main, LV_LABEL_LONG_DOT);
        lv_obj_set_width(w_main, 105);
        lv_obj_align(w_main, LV_ALIGN_LEFT_MID, 341, 0);
        lv_obj_t* w_info = lv_label_create(bar);
        pu_weather_info  = w_info;
        lv_label_set_text(w_info, "--\xC2\xB0\x43 / ---\xC2\xB0\x46  --%");
        lv_obj_set_style_text_color(w_info, lv_color_make(0x88, 0xCC, 0xFF), 0);
        lv_obj_set_style_text_font(w_info, &lv_font_montserrat_22, 0);
        lv_obj_align(w_info, LV_ALIGN_LEFT_MID, 451, 0);

        /* --- City name (right-aligned, enlarged font, auto-truncate) --- */
        lv_obj_t* w_city = lv_label_create(bar);
        pu_weather_city  = w_city;
        lv_label_set_text(w_city, "");
        lv_obj_set_style_text_color(w_city, lv_color_make(0x66, 0x88, 0xAA), 0);
        lv_obj_set_style_text_font(w_city, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(w_city, LV_LABEL_LONG_DOT);
        lv_obj_set_width(w_city, 170);
        lv_obj_set_style_text_align(w_city, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(w_city, LV_ALIGN_RIGHT_MID, -10, 0);
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
        char      ftr_buf[80];
#ifdef CONFIG_USB_CDC_MODE
        snprintf(ftr_buf, sizeof(ftr_buf), " System Monitor  |  USB Connected  |  PC Dashboard v3");
#else
        snprintf(ftr_buf, sizeof(ftr_buf), " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
#endif
        lv_label_set_text(ftr_lbl, ftr_buf);
        lv_obj_set_style_text_color(ftr_lbl, lv_color_make(0x66, 0x88, 0xAA), 0);
        lv_obj_set_style_text_font(ftr_lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ftr_lbl, LV_ALIGN_LEFT_MID, 15, 0);
        g_mqtt_status_label = ftr_lbl; /* Dynamic MQTT status update */
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

void update_layout_pulse(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    /* Note: has_data may be false after pc_stats_reset_to_default() on timeout.
     * We still run the update so reset (zero/placeholder) values are rendered
     * on screen instead of frozen stale data. */

    /* ---- CPU box ---- */
    if (pu_cpu_val)
    {
        int val = (int) (stats.cpu + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
        bool over       = s_cpu_data_seen && ((stats.cpu > g_flash_threshold.cpu_pct) || (stats.cpu_temp_valid && stats.cpu_temp > g_flash_threshold.cpu_temp_c));
        g_cpu_over      = over;
        lv_opa_t opa    = (over && !g_flash_on) ? LV_OPA_40 : LV_OPA_COVER;
        if (pu_cpu_val)
            lv_obj_set_style_text_opa(pu_cpu_val, opa, 0);
    }

    /* ---- RAM box ---- */
    if (pu_ram_val)
    {
        int val = (int) (stats.mem + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
            s_last_mem_used  = stats.mem_used;
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
        int val = (int) (stats.disk + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
        int val = (int) (stats.battery_percent + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
                int gval = (int) (usage + 0.5f);
                if (gval > 100)
                    gval = 100;
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
        bool mem_changed  = (stats.gpu_mem_used_mb != s_last_gpu_mem);

        if (name_changed || temp_changed || mem_changed)
        {
            if (strlen(stats.gpu_name) > 0)
            {
                char sub_buf[64] = "";
                if (stats.gpu_temp_c >= 0)
                    snprintf(sub_buf, sizeof(sub_buf), "%.0f \xC2\xB0\x43", stats.gpu_temp_c);
                if (stats.gpu_mem_used_mb >= 0)
                    snprintf(sub_buf + strlen(sub_buf), sizeof(sub_buf) - strlen(sub_buf), "  %.0f MB", stats.gpu_mem_used_mb);
                lv_label_set_text(pu_gpu_sub, sub_buf);
            }
            else
            {
                lv_label_set_text(pu_gpu_sub, "GPU N/A");
            }
            strncpy(s_last_gpu_name, stats.gpu_name, sizeof(s_last_gpu_name) - 1);
            s_last_gpu_temp = stats.gpu_temp_c;
            s_last_gpu_mem  = stats.gpu_mem_used_mb;
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
            snprintf(net_buf, sizeof(net_buf), "TX: %.1f KB/s  RX: %.1f KB/s", tx, rx);
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
            lv_label_set_text_fmt(pu_sys_p, "Procs: %d", (int) pc);
            s_last_proc_cnt = pc;
        }
    }
    if (pu_sys_c)
    {
        uint8_t cores = stats.cpu_cores_logical;
        if (cores != s_last_cores)
        {
            lv_label_set_text_fmt(pu_sys_c, "Cores: %dP / %dL", (int) stats.cpu_cores_physical, (int) stats.cpu_cores_logical);
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
                uint8_t  mo, d, h, mi, s;
                unix_to_datetime(stats.boot_time + UTC8_OFFSET_SEC, &y, &mo, &d, &h, &mi, &s);
                lv_label_set_text_fmt(pu_sys_b, "Boot: %04d-%02d-%02d %02d:%02d", (int) y, (int) mo, (int) d, (int) h, (int) mi);
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
        if (stats.sht3x_temperature != s_last_env_temp || (int) stats.sht3x_humidity != s_last_env_humi)
        {
            int f = (int) (stats.sht3x_temperature * 9.0f / 5.0f + 32.0f + 0.5f);
            lv_label_set_text_fmt(pu_env_t, "%.1f\xC2\xB0\x43 / %d\xC2\xB0\x46  %d%%", stats.sht3x_temperature, f, (int) stats.sht3x_humidity);
            s_last_env_temp = stats.sht3x_temperature;
            s_last_env_humi = (int) stats.sht3x_humidity;
        }
    }
    /* pu_env_h intentionally left NULL — humidity folded into pu_env_t */
    /* Threshold guard — evaluate only after each datum has been received at least once */
    if (stats.sht3x_valid)
        s_env_data_seen = true;
    if (stats.mem > 0)
        s_ram_data_seen = true;
    if (stats.disk > 0)
        s_disk_data_seen = true;
    if (stats.battery_percent > 0 || stats.battery_plugged)
        s_bat_data_seen = true;
    if (stats.gpu_usage > 0 || strlen(stats.gpu_name) > 0)
        s_gpu_data_seen = true;

    g_env_over  = s_env_data_seen && (stats.sht3x_valid && stats.sht3x_temperature > g_flash_threshold.env_temp_c);
    g_ram_over  = s_ram_data_seen && (stats.mem > g_flash_threshold.ram_pct);
    g_disk_over = s_disk_data_seen && (stats.disk > g_flash_threshold.disk_pct);
    g_bat_over  = s_bat_data_seen && (stats.battery_percent > 0 && !stats.battery_plugged && stats.battery_percent < g_flash_threshold.bat_low_pct);
    g_gpu_over  = s_gpu_data_seen && (stats.gpu_usage > g_flash_threshold.gpu_pct);

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

