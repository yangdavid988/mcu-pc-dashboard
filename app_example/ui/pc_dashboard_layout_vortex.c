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
    const theme_t* th    = &g_themes[g_theme_id];
    const char*    tname = theme_get_name(g_theme_id);
    const char*    lname = layout_get_name(LAYOUT_VORTEX);

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
#define VORTEX_LP_W 260 /* left panel */
#define VORTEX_CP_W 260 /* center panel */
#define VORTEX_RP_W 260 /* right panel */

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
    snprintf(title_buf, sizeof(title_buf), "PC DASHBOARD - %s - %s", lname, tname);
    lv_obj_t* title_lbl = lv_label_create(header);
    lv_label_set_text(title_lbl, title_buf);
    lv_obj_set_style_text_color(title_lbl, th->header, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 52, 0);

    /* User name label */
    create_icon_img(header, &icon_user, lv_color_make(0x88, 0xAA, 0xCC), 440, 2);
    lv_obj_t* user_lbl = lv_label_create(header);
    vo_user            = user_lbl;
    lv_label_set_text(user_lbl, "");
    lv_obj_set_style_text_color(user_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(user_lbl, 470, 4);

    /* Clock label */
    lv_obj_t* clock_lbl = lv_label_create(header);
    vo_time             = clock_lbl;
    lv_label_set_text(clock_lbl, "--:--:--");
    lv_obj_set_style_text_color(clock_lbl, lv_color_make(0x00, 0xFF, 0x88), 0);
    lv_obj_set_style_text_font(clock_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(clock_lbl, LV_ALIGN_RIGHT_MID, -2, 0);

    /* NO DATA warning */
    lv_obj_t* warn_icon = create_icon_img(header, &icon_warning, th->warn, 680, 2);
    vo_warn_icon        = warn_icon;
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
        int cw  = VORTEX_LP_W - TRIAD_CARD_PADDING_H;
        int bw  = cw - TRIAD_CARD_BAR_MARGIN;
        int ch  = 120; /* 3 cards: top + 3*h + 2*gap = 376 */
        int ct  = TRIAD_CARD_TOP_OFFSET;
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
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct);

            create_icon_img(card, &icon_ram, accent, 6, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "RAM");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 40, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_ram_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            vo_ram_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* used = lv_label_create(card);
            vo_ram_swap    = used;
            lv_label_set_text(used, "");
            lv_obj_set_style_text_color(used, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(used, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(used, 6, 60);

            lv_obj_t* swap = lv_label_create(card);
            vo_ram_swap2   = swap;
            lv_label_set_text(swap, "");
            lv_obj_set_style_text_color(swap, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(swap, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(swap, 140, 60);
        }

        /* ---- DISK card ---- */
        {
            lv_color_t accent = th->disk;
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct + (ch + gap));

            create_icon_img(card, &icon_disk, accent, 6, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "DISK");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 40, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_dsk_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            vo_dsk_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* io = lv_label_create(card);
            vo_dsk_io    = io;
            lv_label_set_text(io, "");
            lv_obj_set_style_text_color(io, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(io, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(io, 6, 60);
        }

        /* ---- BATT card ---- */
        {
            lv_color_t accent = th->batt;
            lv_obj_t*  card   = create_card(lp, cw, ch, accent, ct + (ch + gap) * 2);

            create_icon_img(card, &icon_battery, accent, 6, 6);
            lv_obj_t* lbl = lv_label_create(card);
            lv_label_set_text(lbl, "BATT");
            lv_obj_set_style_text_color(lbl, accent, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, 40, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_bat_val    = val;
            lv_label_set_text(val, "0%");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -6, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, TRIAD_CARD_BAR_H, lv_color_make(0x22, 0x22, 0x35), accent);
            vo_bat_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* indicator = lv_obj_create(card);
            lv_obj_set_size(indicator, 8, 8);
            lv_obj_set_pos(indicator, 8, 70);
            lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(indicator, 0, 0);
            lv_obj_set_style_bg_color(indicator, lv_color_make(0x44, 0xDD, 0x44), 0);
            lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
            vo_bat_icon    = indicator;
            lv_obj_t* stat = lv_label_create(card);
            vo_bat_sts     = stat;
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
        int cp_x    = VORTEX_LP_W + LAYOUT_COL_GAP; /* 176 */
        int ct      = 11;                           /* Center top offset for ring (shifted up 5px) */
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
            int ring_x    = (VORTEX_CP_W - ring_sz) / 2;
            vo_cpu_canvas = lv_canvas_create(cp);
            lv_obj_set_size(vo_cpu_canvas, ring_sz, ring_sz);
            lv_obj_set_pos(vo_cpu_canvas, ring_x, ct);
            lv_obj_set_style_radius(vo_cpu_canvas, ring_sz / 2, 0);
            lv_obj_set_style_clip_corner(vo_cpu_canvas, true, 0);
            /* Canvas has no border/shadow (provided by ring_frame overlay) */
            lv_obj_set_style_border_width(vo_cpu_canvas, 0, 0);
            lv_obj_set_style_shadow_width(vo_cpu_canvas, 0, 0);
            s_cpu_canvas_draw_buf = lv_draw_buf_create(ring_sz, ring_sz, LV_COLOR_FORMAT_ARGB8888, 0);
            if (s_cpu_canvas_draw_buf == NULL)
            {
                RTK_LOGE(TAG, "CPU canvas: lv_draw_buf_create(%dx%d) failed\n", (int) ring_sz, (int) ring_sz);
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
                if (db && db->data)
                {
                    lv_color32_t* cp_px = (lv_color32_t*) db->data;
                    lv_color32_t   bg    = { .red = 0x12, .green = 0x12, .blue = 0x26, .alpha = 0xFF };
                    for (uint32_t i = 0; i < (uint32_t) (ring_sz * ring_sz); i++)
                        cp_px[i] = bg;
                }
            }

            s_current_pct = 0.0f;
            s_target_pct  = 0.0f;
            s_anim_phase  = 0;
            if (s_particle_timer == NULL)
            {
                s_particle_timer = lv_timer_create(cpu_particle_timer_cb, 100, NULL);
            }
        }

        /* ---- Frequency & Temperature labels below ring ---- */
        {
            int fy = ct + ring_sz + 4;

            lv_obj_t* freq = lv_label_create(cp);
            vo_cpu_freq    = freq;
            lv_label_set_text(freq, "Freq: N/A");
            lv_obj_set_style_text_color(freq, lv_color_make(0xAA, 0xCC, 0xEE), 0);
            lv_obj_set_style_text_font(freq, &lv_font_montserrat_18, 0);
            // lv_obj_set_pos(freq, 10, fy);
            lv_obj_align(freq, LV_ALIGN_TOP_MID, 0, fy);

            lv_obj_t* temp = lv_label_create(cp);
            vo_cpu_temp    = temp;
            lv_label_set_text(temp, "Temp: N/A");
            lv_obj_set_style_text_color(temp, lv_color_make(0xAA, 0xCC, 0xEE), 0);
            lv_obj_set_style_text_font(temp, &lv_font_montserrat_18, 0);
            lv_obj_align(temp, LV_ALIGN_TOP_MID, 0, fy + 24);
            // lv_obj_set_pos(temp, 10, fy + 20);
        }

        /* ---- GPU card ---- */
        {
            lv_color_t accent = th->gpu;
            int        gpu_y  = ct + ring_sz + 12 + 22 + 12 + 18; /* ~202 */
            int        gpu_ch = TRIAD_PANEL_HEIGHT - gpu_y - 4;
            int        cw     = VORTEX_CP_W - TRIAD_CARD_PADDING_H;
            int        bw     = cw - TRIAD_CARD_BAR_MARGIN;
            int        bh     = 14;

            lv_obj_t* card = create_card(cp, cw, gpu_ch, accent, gpu_y);

            create_icon_img(card, &icon_gpu, accent, 8, 6);
            lv_obj_t* gpu_title = lv_label_create(card);
            lv_label_set_text(gpu_title, "GPU");
            lv_obj_set_style_text_color(gpu_title, accent, 0);
            lv_obj_set_style_text_font(gpu_title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(gpu_title, 44, 16);

            lv_obj_t* val = lv_label_create(card);
            vo_gpu_val    = val;
            lv_label_set_text(val, "");
            lv_obj_set_style_text_color(val, accent, 0);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
            lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -8, 10);

            lv_obj_t* bar = create_glow_bar(card, bw, bh, lv_color_make(0x22, 0x22, 0x35), accent);
            vo_gpu_bar    = bar;
            lv_obj_set_pos(bar, 5, 42);

            lv_obj_t* name = lv_label_create(card);
            vo_gpu_name    = name;
            lv_label_set_text(name, "");
            lv_obj_set_style_text_color(name, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(name, 8, 62);

            lv_obj_t* temp_mem = lv_label_create(card);
            vo_gpu_tm          = temp_mem;
            lv_label_set_text(temp_mem, "");
            lv_obj_set_style_text_color(temp_mem, lv_color_make(0xAA, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(temp_mem, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(temp_mem, 8, 84);
        }
    }

    /* ==============================================================
     * 5. Right panel — NETWORK (compact) + SYSTEM
     * ============================================================== */
    {
        int rp_x   = VORTEX_LP_W + LAYOUT_COL_GAP + VORTEX_CP_W + LAYOUT_COL_GAP;
        int cw     = VORTEX_RP_W - TRIAD_CARD_PADDING_H;
        int ct     = TRIAD_CARD_TOP_OFFSET;
        int gap    = TRIAD_CARD_GAP;
        int ch_net = 110; /* compact NETWORK card (was 80, too small for 3x 32px icons) */

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
            lv_obj_t*  card   = create_card(rp, cw, ch_net, accent, ct);

            create_icon_img(card, &icon_wifi, accent, 6, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "NETWORK");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 40, 16);

            /* TX */
            create_icon_img(card, &icon_arrow_up, lv_color_make(0x88, 0xDD, 0xAA), 8, 35);
            lv_obj_t* up_lbl = lv_label_create(card);
            vo_net_tx        = up_lbl;
            lv_label_set_text(up_lbl, "");
            lv_obj_set_style_text_color(up_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(up_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(up_lbl, 48, 46);

            /* RX */
            create_icon_img(card, &icon_arrow_down, lv_color_make(0x88, 0xDD, 0xAA), 8, 69);
            lv_obj_t* down_lbl = lv_label_create(card);
            vo_net_rx          = down_lbl;
            lv_label_set_text(down_lbl, "");
            lv_obj_set_style_text_color(down_lbl, lv_color_make(0x88, 0xDD, 0xAA), 0);
            lv_obj_set_style_text_font(down_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(down_lbl, 48, 80);
        }

        /* ---- SYSTEM card ---- */
        {
            int        ch_sys = TRIAD_PANEL_HEIGHT - ct - ch_net - gap - 4;
            lv_color_t accent = th->sys;
            lv_obj_t*  card   = create_card(rp, cw, ch_sys, accent, ct + ch_net + gap);

            create_icon_img(card, &icon_gear, accent, 6, 6);
            lv_obj_t* title = lv_label_create(card);
            lv_label_set_text(title, "SYSTEM");
            lv_obj_set_style_text_color(title, accent, 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 40, 16);

            int lx   = 6;
            int ly   = 50;
            int lgap = 38;

            /* Processes */
            create_icon_img(card, &icon_list, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11);
            lv_obj_t* proc_lbl = lv_label_create(card);
            vo_sys_p           = proc_lbl;
            lv_label_set_text(proc_lbl, "");
            lv_obj_set_style_text_color(proc_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(proc_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(proc_lbl, lx + 40, ly);

            /* Cores */
            create_icon_img(card, &icon_cpu, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap);
            lv_obj_t* cores_lbl = lv_label_create(card);
            vo_sys_c            = cores_lbl;
            lv_label_set_text(cores_lbl, "");
            lv_obj_set_style_text_color(cores_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(cores_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(cores_lbl, lx + 40, ly + lgap);

            /* Boot time */
            create_icon_img(card, &icon_power_off, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 2);
            lv_obj_t* boot_lbl = lv_label_create(card);
            vo_sys_b           = boot_lbl;
            lv_label_set_text(boot_lbl, "");
            lv_obj_set_style_text_color(boot_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(boot_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(boot_lbl, lx + 40, ly + lgap * 2 - 2);

            /* Hostname */
            create_icon_img(card, &icon_user, lv_color_make(0xCC, 0xDD, 0xEE), lx, ly - 11 + lgap * 3);
            lv_obj_t* host_lbl = lv_label_create(card);
            vo_sys_h           = host_lbl;
            lv_label_set_text(host_lbl, "");
            lv_obj_set_style_text_color(host_lbl, lv_color_make(0xCC, 0xDD, 0xEE), 0);
            lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_pos(host_lbl, lx + 40, ly + lgap * 3 - 2);

            /* OS */
            create_icon_img(card, &icon_globe, lv_color_make(0x88, 0xAA, 0xCC), lx, ly - 11 + lgap * 4);
            lv_obj_t* os_lbl = lv_label_create(card);
            vo_sys_o         = os_lbl;
            lv_label_set_text(os_lbl, "OS: N/A");
            lv_obj_set_style_text_color(os_lbl, lv_color_make(0x88, 0xAA, 0xCC), 0);
            lv_obj_set_style_text_font(os_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_width(os_lbl, cw - 24);
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

        /* --- Indoor (SHT3X): temp icon + "28.5°C/82°F  60%" --- */
        lv_obj_t* temp_lbl = lv_label_create(bar);
        vo_env_t           = temp_lbl;
        lv_label_set_text(temp_lbl, "--.-\xC2\xB0\x43 / ---\xC2\xB0\x46  --%");
        lv_obj_set_style_text_color(temp_lbl, lv_color_make(0xAA, 0xFF, 0xCC), 0);
        lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_22, 0);
        lv_obj_align(temp_lbl, LV_ALIGN_LEFT_MID, 56, 0);
        /* vo_env_h intentionally left NULL — humidity folded into vo_env_t */

        /* --- Outdoor weather: icon + "Clear" + "28°C / 82°F  65%" --- */
        vo_weather_icon  = create_icon_img(bar, &icon_sun, lv_color_make(0x88, 0xCC, 0xFF), 305, 0);
        lv_obj_t* w_main = lv_label_create(bar);
        vo_weather_main  = w_main;
        lv_label_set_text(w_main, "");
        lv_obj_set_style_text_color(w_main, lv_color_make(0x88, 0xCC, 0xFF), 0);
        lv_obj_set_style_text_font(w_main, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(w_main, LV_LABEL_LONG_DOT);
        lv_obj_set_width(w_main, 105);
        lv_obj_align(w_main, LV_ALIGN_LEFT_MID, 341, 0);
        lv_obj_t* w_info = lv_label_create(bar);
        vo_weather_info  = w_info;
        lv_label_set_text(w_info, "--\xC2\xB0\x43 / ---\xC2\xB0\x46  --%");
        lv_obj_set_style_text_color(w_info, lv_color_make(0x88, 0xCC, 0xFF), 0);
        lv_obj_set_style_text_font(w_info, &lv_font_montserrat_22, 0);
        lv_obj_align(w_info, LV_ALIGN_LEFT_MID, 451, 0);

        /* --- City name (right-aligned, enlarged font, auto-truncate) --- */
        lv_obj_t* w_city = lv_label_create(bar);
        vo_weather_city  = w_city;
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
        g_mqtt_status_label = ftr_lbl;
    }
}

void update_layout_vortex(void)
{
    PC_Stats_t stats;
    taskENTER_CRITICAL();
    memcpy(&stats, &g_pc_stats, sizeof(PC_Stats_t));
    taskEXIT_CRITICAL();

    /* Note: has_data may be false after pc_stats_reset_to_default() on timeout.
     * We still run the update so reset (zero/placeholder) values are rendered
     * on screen instead of frozen stale data. */

    /* ---- CPU ring (Canvas) ---- */
    if (vo_cpu_canvas)
    {
        int val = (int) (stats.cpu + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;
        if (val != s_last_cpu_pct)
        {
            s_target_pct   = (float) val;
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
        bool over       = s_cpu_data_seen && ((stats.cpu > g_flash_threshold.cpu_pct) || (stats.cpu_temp_valid && stats.cpu_temp > g_flash_threshold.cpu_temp_c));
        g_cpu_over      = over;
    }

    /* ---- RAM ---- */
    if (vo_ram_val || vo_ram_bar)
    {
        int val = (int) (stats.mem + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
            s_last_mem_used  = stats.mem_used;
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
        int val = (int) (stats.disk + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;

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
        int val = (int) (stats.battery_percent + 0.5f);
        if (val > 100)
            val = 100;
        if (val < 0)
            val = 0;
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
        } /* if val != s_last_bat_pct */
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
                                              lv_color_make(0x44, 0xDD, 0x44),
                                              0); /* green */
                else
                    lv_obj_set_style_bg_color(vo_bat_icon,
                                              lv_color_make(0xFF, 0x33, 0x33),
                                              0); /* red */
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
                int gval = (int) (stats.gpu_usage + 0.5f);
                if (gval > 100)
                    gval = 100;
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
        bool mem_changed  = (stats.gpu_mem_used_mb != s_last_gpu_mem);
        if (name_changed || temp_changed || mem_changed)
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
                lv_label_set_text_fmt(vo_gpu_tm, "%s  %s", temp_str, mem_str);
            }
            else
            {
                lv_label_set_text(vo_gpu_tm, "No GPU data available");
            }
            strncpy(s_last_gpu_name, stats.gpu_name, sizeof(s_last_gpu_name) - 1);
            s_last_gpu_temp = stats.gpu_temp_c;
            s_last_gpu_mem  = stats.gpu_mem_used_mb;
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
            lv_label_set_text_fmt(vo_sys_p, "Processes: %d", (int) pc);
            s_last_proc_cnt = pc;
        }
    }
    if (vo_sys_c)
    {
        uint8_t cores = stats.cpu_cores_logical;
        if (cores != s_last_cores)
        {
            lv_label_set_text_fmt(vo_sys_c, "Cores: %dP / %dL", (int) stats.cpu_cores_physical, (int) stats.cpu_cores_logical);
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
                uint8_t  mo, d, h, mi, s;
                unix_to_datetime(stats.boot_time + UTC8_OFFSET_SEC, &y, &mo, &d, &h, &mi, &s);
                lv_label_set_text_fmt(vo_sys_b, "Boot: %04d-%02d-%02d %02d:%02d", (int) y, (int) mo, (int) d, (int) h, (int) mi);
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
    } /* end of if (vo_cpu_canvas) */

    /* ---- ENV ---- */
    if (vo_env_t && stats.sht3x_valid)
    {
        if (stats.sht3x_temperature != s_last_env_temp || (int) stats.sht3x_humidity != s_last_env_humi)
        {
            int f = (int) (stats.sht3x_temperature * 9.0f / 5.0f + 32.0f + 0.5f);
            lv_label_set_text_fmt(vo_env_t, "%.1f\xC2\xB0\x43 / %d\xC2\xB0\x46  %d%%", stats.sht3x_temperature, f, (int) stats.sht3x_humidity);
            s_last_env_temp = stats.sht3x_temperature;
            s_last_env_humi = (int) stats.sht3x_humidity;
        }
    }
    /* vo_env_h intentionally left NULL — humidity folded into vo_env_t */
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

    g_env_over  = s_env_data_seen && stats.sht3x_valid && (stats.sht3x_temperature > g_flash_threshold.env_temp_c);
    g_ram_over  = s_ram_data_seen && (stats.mem > g_flash_threshold.ram_pct);
    g_disk_over = s_disk_data_seen && (stats.disk > g_flash_threshold.disk_pct);
    g_bat_over  = s_bat_data_seen && (stats.battery_percent > 0 && !stats.battery_plugged && stats.battery_percent < g_flash_threshold.bat_low_pct);
    g_gpu_over  = s_gpu_data_seen && (stats.gpu_usage > g_flash_threshold.gpu_pct);

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

