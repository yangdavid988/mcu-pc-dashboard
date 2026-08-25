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

void create_layout_triad(lv_obj_t* parent)
{
    const theme_t* th    = &g_themes[g_theme_id];
    const char*    tname = theme_get_name(g_theme_id);
    const char*    lname = layout_get_name(LAYOUT_TRIAD);

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
    snprintf(title_buf, sizeof(title_buf), "PC DASHBOARD - %s - %s", lname, tname);
    lv_obj_t* title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title_buf);
    lv_obj_set_style_text_color(title_lbl, th->header, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 52, 0);

    /* User name label (populated by update function) */
    create_icon_img(header, &icon_user, lv_color_make(0x88, 0xAA, 0xCC), 430, 2);
    lv_obj_t* user_lbl = lv_label_create(header);
    tr_user            = user_lbl;
    lv_label_set_text(user_lbl, "");
    lv_obj_set_style_text_color(user_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(user_lbl, 460, 4);

    /* Clock label */
    lv_obj_t* clock_lbl = lv_label_create(header);
    tr_time             = clock_lbl;
    lv_label_set_text(clock_lbl, "--:--:--");
    lv_obj_set_style_text_color(clock_lbl, lv_color_make(0x00, 0xFF, 0x88), 0);
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -2, 0);

    /* NO DATA warning icon + label (hidden by default, toggled by update) */
    lv_obj_t* warn_icon = create_icon_img(header, &icon_warning, th->warn, 680, 2);
    tr_warn_icon        = warn_icon;
    lv_obj_add_flag(warn_icon, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* warn_lbl = lv_label_create(header);
    tr_warn_lbl        = warn_lbl;
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
        int pw  = 260;
        int cw  = pw - TRIAD_CARD_PADDING_H;
        int bw  = cw - TRIAD_CARD_BAR_MARGIN;
        int ch  = TRIAD_CARD_LEFT_CH;
        int gap = TRIAD_CARD_GAP;
        int ct  = TRIAD_CARD_TOP_OFFSET;

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
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct);

            create_icon_img(card, &icon_cpu, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "CPU");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_cpu_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            tr_cpu_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* freq = lv_label_create(card);
            tr_cpu_freq    = freq;
            lv_label_set_text(freq, "");
            lv_obj_set_style_text_color(freq, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(freq, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(freq, 8, 62);

            lv_obj_t* temp = lv_label_create(card);
            tr_cpu_temp    = temp;
            lv_label_set_text(temp, "");
            lv_obj_set_style_text_color(temp, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(temp, 150, 62);
        }

        /* ---- RAM card ---- */
        {
            lv_color_t accent = th->ram;
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct + (ch + gap));

            create_icon_img(card, &icon_ram, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "RAM");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_ram_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            tr_ram_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* swap = lv_label_create(card);
            tr_ram_swap    = swap;
            lv_label_set_text(swap, "");
            lv_obj_set_style_text_color(swap, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(swap, 8, 62);

            lv_obj_t* swap2 = lv_label_create(card);
            tr_ram_swap2    = swap2;
            lv_label_set_text(swap2, "");
            lv_obj_set_style_text_color(swap2, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap2, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(swap2, 150, 62);
        }

        /* ---- DISK card ---- */
        {
            lv_color_t accent = th->disk;
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct + (ch + gap) * 2);

            create_icon_img(card, &icon_disk, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "DISK");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_dsk_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            tr_dsk_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* io = lv_label_create(card);
            tr_dsk_io    = io;
            lv_label_set_text(io, "");
            lv_obj_set_style_text_color(io, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(io, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(io, 8, 62);
        }

        /* ---- BATT card ---- */
        {
            lv_color_t accent = th->batt;
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct + (ch + gap) * 3);

            create_icon_img(card, &icon_battery, accent, 8, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "BATT");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 42, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_bat_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            tr_bat_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            /* Status indicator circle + label */
            lv_obj_t* indicator = lv_obj_create(card);
            lv_obj_set_size(indicator, 8, 8);
            lv_obj_set_pos(indicator, 10, 66);
            lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(indicator, 0, 0);
            lv_obj_set_style_bg_color(indicator, lv_color_make(0x44, 0xDD, 0x44), 0);
            lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
            tr_bat_icon    = indicator;
            lv_obj_t* stat = lv_label_create(card);
            tr_bat_sts     = stat;
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
        int pw     = 260;
        int cw     = pw - TRIAD_CARD_PADDING_H;
        int bw     = cw - TRIAD_CARD_BAR_MARGIN;
        int bh     = TRIAD_CARD_BAR_H;
        int gap    = TRIAD_CARD_MID_GAP;
        int ct     = TRIAD_CARD_TOP_OFFSET;
        int ch_gpu = (TRIAD_PANEL_HEIGHT - ct - gap) / 2;
        int ch_io  = TRIAD_PANEL_HEIGHT - ct - ch_gpu - gap - 4;

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
            lv_obj_t*  card   = create_card(mp, cw, ch_gpu, accent, ct);

            create_icon_img(card, &icon_gpu, accent, 8, 6);
            lv_obj_t* gpu_title = lv_label_create(card);
            lv_label_set_text(gpu_title, "GPU");
            lv_obj_set_style_text_color(gpu_title, accent, 0);
            lv_obj_set_style_text_font(gpu_title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(gpu_title, 44, 16);

            lv_obj_t* val = lv_label_create(card);
            tr_gpu_val    = val;
            lv_label_set_text(val, "");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, bh, lv_color_make(0x22, 0x22, 0x35), accent);
            tr_gpu_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* name = lv_label_create(card);
            tr_gpu_name    = name;
            lv_label_set_text(name, "");
            lv_obj_set_style_text_color(name, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(name, 8, 62);

            lv_obj_t* temp_mem = lv_label_create(card);
            tr_gpu_tm          = temp_mem;
            lv_label_set_text(temp_mem, "");
            lv_obj_set_style_text_color(temp_mem, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp_mem, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(temp_mem, 8, 84);
        }

        /* ---- DISK I/O card ---- */
        {
            lv_color_t accent = th->io;
            lv_obj_t*  card   = create_card(mp, cw, ch_io, accent, ct + ch_gpu + gap);

            lv_obj_t* io_title = lv_label_create(card);
            lv_label_set_text(io_title, "DISK I/O");
            lv_obj_set_style_text_color(io_title, accent, 0);
            lv_obj_set_style_text_font(io_title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(io_title, 42, 6);

            /* HDD icon + two-line read/write labels */
            create_icon_img(card, &icon_disk, accent, 8, 2);
            lv_obj_t* read_lbl = lv_label_create(card);
            tr_io_read         = read_lbl;
            lv_label_set_text(read_lbl, "");
            lv_obj_set_style_text_color(read_lbl, lv_color_make(0x88, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(read_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(read_lbl, 44, 40);

            lv_obj_t* write_lbl = lv_label_create(card);
            tr_io_write         = write_lbl;
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
        int pw     = 260;
        int cw     = pw - TRIAD_CARD_PADDING_H;
        int gap    = TRIAD_CARD_GAP;
        int ct     = TRIAD_CARD_TOP_OFFSET;
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
            lv_obj_t*  card   = create_card(rp, cw, ch_net, accent, ct);

            create_icon_img(card, &icon_wifi, accent, 8, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "NETWORK");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 42, 16);

            /* TX icon + label */
            create_icon_img(card, &icon_arrow_up, lv_color_make(0x88, 0xDD, 0xAA), 10, 38);
            lv_obj_t* up_lbl = lv_label_create(card);
            tr_net_tx        = up_lbl;
            lv_label_set_text(up_lbl, "");
            lv_obj_set_style_text_color(up_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(up_lbl, 54, 50);

            /* RX icon + label */
            create_icon_img(card, &icon_arrow_down, lv_color_make(0x88, 0xDD, 0xAA), 10, 76);
            lv_obj_t* down_lbl = lv_label_create(card);
            tr_net_rx          = down_lbl;
            lv_label_set_text(down_lbl, "");
            lv_obj_set_style_text_color(down_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(down_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(down_lbl, 54, 88);
        }

        /* ---- SYSTEM card ---- */
        {
            lv_color_t accent = th->sys;
            lv_obj_t*  card   = create_card(rp, cw, ch_sys, accent, ct + ch_net + gap);

            create_icon_img(card, &icon_gear, accent, 8, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "SYSTEM");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 42, 16);

            int lx   = 8;
            int ly   = 50;
            int lgap = 38;

            /* Processes */
            create_icon_img(card, &icon_list, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11);
            lv_obj_t* proc_lbl = lv_label_create(card);
            tr_sys_p           = proc_lbl;
            lv_label_set_text(proc_lbl, "");
            lv_obj_set_style_text_color(proc_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(proc_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(proc_lbl, lx + 40, ly);

            /* Cores */
            create_icon_img(card, &icon_cpu, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap);
            lv_obj_t* cores_lbl = lv_label_create(card);
            tr_sys_c            = cores_lbl;
            lv_label_set_text(cores_lbl, "");
            lv_obj_set_style_text_color(cores_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(cores_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(cores_lbl, lx + 40, ly + lgap);

            /* Boot time */
            create_icon_img(card, &icon_power_off, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 2);
            lv_obj_t* boot_lbl = lv_label_create(card);
            tr_sys_b           = boot_lbl;
            lv_label_set_text(boot_lbl, "");
            lv_obj_set_style_text_color(boot_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(boot_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(boot_lbl, lx + 40, ly + lgap * 2 - 2);

            /* Hostname */
            create_icon_img(card, &icon_user, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 3);
            lv_obj_t* host_lbl = lv_label_create(card);
            tr_sys_h           = host_lbl;
            lv_label_set_text(host_lbl, "");
            lv_obj_set_style_text_color(host_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(host_lbl, lx + 40, ly + lgap * 3 - 2);

            /* OS */
            create_icon_img(card, &icon_globe, lv_color_make(0x88, 0xAA, 0xCC), lx, ly - 11 + lgap * 4);
            lv_obj_t* os_lbl = lv_label_create(card);
            tr_sys_o         = os_lbl;
            lv_label_set_text(os_lbl, "OS: N/A");
            lv_obj_set_style_text_color(os_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(os_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_width(os_lbl, cw - 26);
            lv_obj_set_pos(os_lbl, lx + 40, ly + lgap * 4 - 2);
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

        /* --- Indoor (SHT3X): temp icon + "28.5°C/82°F  60%" (no text label) --- */
        lv_obj_t* temp_lbl = lv_label_create(bar);
        tr_env_t           = temp_lbl;
        lv_label_set_text(temp_lbl, "--.-\xC2\xB0\x43 / ---\xC2\xB0\x46  --%");
        lv_obj_set_style_text_color(temp_lbl, lv_color_make(0xAA, 0xFF, 0xCC), 0);
        lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_22, 0);
        lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 56, 0);
        /* tr_env_h intentionally left NULL — humidity folded into tr_env_t */

        /* --- Outdoor weather: icon + "Clear" + "28°C / 82°F  65%" --- */
        tr_weather_icon  = create_icon_img(bar, &icon_sun, lv_color_make(0x88, 0xCC, 0xFF), 305, 0);
        lv_obj_t* w_main = lv_label_create(bar);
        tr_weather_main  = w_main;
        lv_label_set_text(w_main, "");
        lv_obj_set_style_text_color(w_main, lv_color_make(0x88, 0xCC, 0xFF), 0);
        lv_obj_set_style_text_font(w_main, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(w_main, LV_LABEL_LONG_DOT);
        lv_obj_set_width(w_main, 105);
        lv_obj_align(w_main, LV_ALIGN_LEFT_MID, 341, 0);
        lv_obj_t* w_info = lv_label_create(bar);
        tr_weather_info  = w_info;
        lv_label_set_text(w_info, "--\xC2\xB0\x43 / ---\xC2\xB0\x46  --%");
        lv_obj_set_style_text_color(w_info, lv_color_make(0x88, 0xCC, 0xFF), 0);
        lv_obj_set_style_text_font(w_info, &lv_font_montserrat_22, 0);
        lv_obj_align(w_info, LV_ALIGN_LEFT_MID, 451, 0);

        /* --- City name (right-aligned, enlarged font, auto-truncate) --- */
        lv_obj_t* w_city = lv_label_create(bar);
        tr_weather_city  = w_city;
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
#ifdef CONFIG_USB_CDC_MODE
        lv_label_set_text(ftr_lbl,
                          " System Monitor  |  USB Connected  |  PC Dashboard v3");
#else
        lv_label_set_text(ftr_lbl,
                          " System Monitor  |  MQTT Connected  |  PC Dashboard v3");
#endif
        lv_obj_set_style_text_color(ftr_lbl, lv_color_make(0x66, 0x88, 0xAA), 0);
        lv_obj_set_style_text_font(ftr_lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(ftr_lbl, LV_ALIGN_LEFT_MID, 15, 0);
        g_mqtt_status_label = ftr_lbl; /* Dynamic MQTT status update */
    }
}


void update_layout_triad(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    /* Note: has_data may be false after pc_stats_reset_to_default() on timeout.
     * We still run the update so reset (zero/placeholder) values are rendered
     * on screen instead of frozen stale data. */

    /* ---- CPU ---- */
    if (tr_cpu_val || tr_cpu_bar)
    {
        int val = (int) (stats.cpu + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
        bool over       = s_cpu_data_seen && ((stats.cpu > g_flash_threshold.cpu_pct) || (stats.cpu_temp_valid && stats.cpu_temp > g_flash_threshold.cpu_temp_c));
        g_cpu_over      = over;
        lv_opa_t opa    = (over && !g_flash_on) ? LV_OPA_40 : LV_OPA_COVER;
        if (tr_cpu_val)
            lv_obj_set_style_text_opa(tr_cpu_val, opa, 0);
        if (tr_cpu_bar)
            lv_obj_set_style_bg_opa(tr_cpu_bar, opa, LV_PART_INDICATOR);
    }

    /* ---- RAM ---- */
    if (tr_ram_val || tr_ram_bar)
    {
        int val = (int) (stats.mem + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
            s_last_mem_used  = stats.mem_used;
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
        int val = (int) (stats.disk + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
                                              lv_color_make(0x44, 0xDD, 0x44),
                                              0); /* green */
                else
                    lv_obj_set_style_bg_color(tr_bat_icon,
                                              lv_color_make(0xFF, 0x33, 0x33),
                                              0); /* red */
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
                int gval = (int) (stats.gpu_usage + 0.5f);
                if (gval > 100)
                    gval = 100;
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
            char mem_str[48]  = "N/A";
            if (stats.gpu_temp_c >= 0)
                snprintf(temp_str, sizeof(temp_str), "%.0f \xC2\xB0\x43", stats.gpu_temp_c);
            if (stats.gpu_mem_used_mb >= 0 && stats.gpu_mem_total_mb >= 0)
                snprintf(mem_str, sizeof(mem_str), "%.0f/%.0f MB", stats.gpu_mem_used_mb, stats.gpu_mem_total_mb);
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
            s_last_io_read  = stats.disk_read_bytes;
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
            lv_label_set_text_fmt(tr_sys_p, "Processes: %d", (int) pc);
            s_last_proc_cnt = pc;
        }
    }
    if (tr_sys_c)
    {
        uint8_t cores = stats.cpu_cores_physical;
        if (cores != s_last_cores)
        {
            lv_label_set_text_fmt(tr_sys_c, "Cores: %dP / %dL", (int) stats.cpu_cores_physical, (int) stats.cpu_cores_logical);
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
                uint8_t  mo, d, h, mi, s;
                unix_to_datetime(stats.boot_time + UTC8_OFFSET_SEC, &y, &mo, &d, &h, &mi, &s);
                lv_label_set_text_fmt(tr_sys_b, "Boot: %04d-%02d-%02d %02d:%02d", (int) y, (int) mo, (int) d, (int) h, (int) mi);
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
        if (stats.sht3x_temperature != s_last_env_temp || (int) stats.sht3x_humidity != s_last_env_humi)
        {
            int f = (int) (stats.sht3x_temperature * 9.0f / 5.0f + 32.0f + 0.5f);
            lv_label_set_text_fmt(tr_env_t, "%.1f\xC2\xB0\x43 / %d\xC2\xB0\x46  %d%%", stats.sht3x_temperature, f, (int) stats.sht3x_humidity);
            s_last_env_temp = stats.sht3x_temperature;
            s_last_env_humi = (int) stats.sht3x_humidity;
        }
    }
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

