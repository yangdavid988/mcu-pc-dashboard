#include "ui/pc_dashboard_lock_screen.h"
#include "core/pc_dashboard.h"
#include "ui/pc_dashboard_theme.h"
#include "ui/pc_dashboard_layout.h"
#include "hal/backlight_ctrl.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#ifndef TAG
#define TAG "LOCK_SCREEN"
#endif

/* ========================================================================
 * Clock face geometry
 *
 * Two screens are supported, each with different pixel geometry:
 *
 * DBL070 pixel pitch: 0.1923mm (H) x 0.1784mm (V)
 *   Ratio = 0.1923 / 0.1784 ≈ 1.0779
 *   To display a physically circular clock, the image is pre-compensated:
 *   width = 400 px, height = 400 × 1.0779 ≈ 432 px (clock432)
 *
 * T1720A pixel pitch: 0.0635mm (H) x 0.1905mm (V)
 *   Ratio = 0.0635 / 0.1905 ≈ 0.3333
 *   V pixels are ~3x taller than H pixels, so a separate 400×400
 *   physically-circular clock face is used (clock400).
 *
 * All hand endpoint Y coordinates are multiplied by PIXEL_RATIO_X1000 / 1000
 * so the hand traces a PHYSICAL circle on the display.
 *
 * The clock sits in a transparent 400 x 480 container at screen centre
 * so the theme gradient / bg_image shows through at the 24 px strips.
 * The clock image fully covers the clock area.
 *
 * The display uses two DIRECT-mode framebuffers with VBlank-synchronised
 * DMA flip.  On each 1 Hz tick the full screen is invalidated, causing
 * LVGL to re-composite the scene into the inactive buffer; the VBlank
 * flip then makes it visible.  Both buffers converge within two refresh
 * cycles (~32 ms), eliminating stale-buffer flicker without needing
 * per-hand dirty-area tracking.
 *
 * Sweep animation (lv_anim_path_ease_out) plays on initial time acquisition:
 * sec 1 s, min 1.5 s, hour 2 s.  The g_animating flag blocks the 1 Hz
 * updater while the animation is running.
 * ======================================================================== */
#define CLOCK_AREA_W      400
#define CLOCK_AREA_H      480
#define LOCAL_CENTER_X    (CLOCK_AREA_W / 2) /* 200 */
#define LOCAL_CENTER_Y    (CLOCK_AREA_H / 2) /* 240 */
#define CLOCK_IMG_W       400
#define CLOCK_RADIUS      200  /* short-dimension radius (half of CLOCK_IMG_W) */

/* Per-screen clock face image and pixel geometry.
 * DBL070 uses clock432 (pre-compensated, 400×432);
 * T1720A uses clock400 (physically circular image, 400×400).
 *
 * PIXEL_RATIO_X1000: hand trace compensation for non-square pixels.
 * DBL070 pixels are slightly wider than tall (0.1923H×0.1784V),
 * so Y is scaled up 1.078× to trace a physical circle on the
 * pre-compensated clock432 image.
 *
 * T1720A pixels are 3× taller than wide (0.0635H×0.1905V), and
 * clock400 is a 400×400 image — the clock face displays as-is
 * (physically stretched).  Hands should match the image geometry,
 * so PIXEL_RATIO=1000 (no extra compensation): the hand trace's
 * pixel aspect ratio matches the displayed image.              */
#ifdef CONFIG_SCREEN_T1720A
#define CLOCK_IMG_H        400       /* physically circular source */
#define PIXEL_RATIO_X1000  1000      /* no compensation — image displays as-is */
#define CLOCK_IMG_DECLARE  LV_IMG_DECLARE(clock400)
#define CLOCK_IMG_SRC      (&clock400)
#else
#define CLOCK_IMG_H        432       /* pre-compensated: 400 × 1.0779 */
#define PIXEL_RATIO_X1000  1078      /* 0.1923 / 0.1784 * 1000 */
#define CLOCK_IMG_DECLARE  LV_IMG_DECLARE(clock432)
#define CLOCK_IMG_SRC      (&clock432)
#endif

/* LV_IMG_DECLARE expands to "extern const lv_image_dsc_t <name>;" — its own ; */
CLOCK_IMG_DECLARE

/* Hand geometry */
#define HAND_SEC_LEN  165
#define HAND_MIN_LEN  130
#define HAND_HOUR_LEN 100
#define HAND_SEC_W    2
#define HAND_MIN_W    5
#define HAND_HOUR_W   8

/* Sweep animation timing (matches LVGL_clock_demo play_ntp_sync_animation) */
#define ANIM_SEC_MS  1000
#define ANIM_MIN_MS  1500
#define ANIM_HOUR_MS 2000

/* ========================================================================
 * Static widget references
 * ======================================================================== */
static lv_obj_t* g_lock_container = NULL;
static lv_obj_t* g_hand_hour      = NULL;
static lv_obj_t* g_hand_min       = NULL;
static lv_obj_t* g_hand_sec       = NULL;
static lv_obj_t* g_date_window    = NULL; /* date capsule at 3 o'clock */
static lv_obj_t* g_date_label     = NULL; /* date number (left half) */
static lv_obj_t* g_week_label     = NULL; /* weekday abbrev (right half) */

static lv_point_precise_t g_pts_hour[2] = { { LOCAL_CENTER_X, LOCAL_CENTER_Y },
                                            { LOCAL_CENTER_X, LOCAL_CENTER_Y - HAND_HOUR_LEN } };
static lv_point_precise_t g_pts_min[2]  = { { LOCAL_CENTER_X, LOCAL_CENTER_Y },
                                            { LOCAL_CENTER_X, LOCAL_CENTER_Y - HAND_MIN_LEN } };
static lv_point_precise_t g_pts_sec[2]  = { { LOCAL_CENTER_X, LOCAL_CENTER_Y },
                                            { LOCAL_CENTER_X, LOCAL_CENTER_Y - HAND_SEC_LEN } };

/* Time tracking */
static uint32_t g_lock_last_second = 0;
static int      g_last_day         = -1;

/* Animation guards */
static bool g_animating  = false; /* blocks 1 Hz updater during sweep */
static bool g_sweep_done = false; /* true once the initial sweep completes */

/* Deferred sweep: create stores the target angles, update starts the
 * animation on the next 1 Hz tick to avoid DIRECT-mode double-buffer
 * desync caused by registering an lv_anim inside the two nested
 * lv_refr_now() calls of create_lock_screen_clock(). */
static bool g_sweep_pending = false;
static int  g_pend_sec      = 0;
static int  g_pend_min      = 0;
static int  g_pend_hour     = 0;

/* PSRAM-cached clock image descriptor (file-level so update()
 * can pre-warm from PSRAM — not flash — before each 1 Hz render). */
static bool         s_img_cached = false;
static lv_img_dsc_t s_img_ram;

/* ========================================================================
 * Day-of-week helper (Tomohiko Sakamoto's algorithm)
 * Returns 0=Sun, 1=Mon ... 6=Sat
 * ======================================================================== */
static int calc_weekday(int y, int m, int d)
{
    static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/* ========================================================================
 * Update hand endpoint using angle (0 deg = 12 o'clock, CW)
 *
 * Y coordinate is multiplied by PIXEL_RATIO to compensate for non-square
 * DBL070 pixels, so the hand traces a physical circle on the display.
 * ======================================================================== */
static void set_hand_angle(lv_point_precise_t pts[2], int len, int angle_deg)
{
    double rad = (double) angle_deg * 3.14159265 / 180.0;
    pts[1].x   = LOCAL_CENTER_X + (int) (len * sin(rad));
    pts[1].y   = LOCAL_CENTER_Y - (int) (len * PIXEL_RATIO_X1000 / 1000.0 * cos(rad));
}

/* ========================================================================
 * Sweep animation callbacks
 * ======================================================================== */
static void sweep_anim_sec_cb(void* var, int32_t v)
{
    LV_UNUSED(var);
    set_hand_angle(g_pts_sec, HAND_SEC_LEN, v);
    lv_line_set_points(g_hand_sec, g_pts_sec, 2);
}

static void sweep_anim_min_cb(void* var, int32_t v)
{
    LV_UNUSED(var);
    set_hand_angle(g_pts_min, HAND_MIN_LEN, v);
    lv_line_set_points(g_hand_min, g_pts_min, 2);
}

static void sweep_anim_hour_cb(void* var, int32_t v)
{
    LV_UNUSED(var);
    set_hand_angle(g_pts_hour, HAND_HOUR_LEN, v);
    lv_line_set_points(g_hand_hour, g_pts_hour, 2);
}

/* ========================================================================
 * Sweep anim ready callback — unblocks the 1 Hz updater
 * ======================================================================== */
static void anim_ready_cb(lv_anim_t* a)
{
    LV_UNUSED(a);
    g_animating = false; /* allow 1 Hz timer to update hands now */
}

/* ========================================================================
 * Start sweep animation for all three hands
 * ======================================================================== */
static void start_sweep_animation(int t_sec, int t_min, int t_hour)
{
    g_animating  = true;
    g_sweep_done = true;

    /* Hands default to 12 o'clock (0 deg) — no pre-set needed */

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, sweep_anim_sec_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    /* Second hand: 0 -> target, 1000 ms */
    lv_anim_set_var(&a, g_hand_sec);
    lv_anim_set_values(&a, 0, t_sec);
    lv_anim_set_time(&a, ANIM_SEC_MS);
    lv_anim_set_ready_cb(&a, NULL);
    lv_anim_start(&a);

    /* Minute hand: 0 -> target, 1500 ms */
    lv_anim_set_exec_cb(&a, sweep_anim_min_cb);
    lv_anim_set_var(&a, g_hand_min);
    lv_anim_set_values(&a, 0, t_min);
    lv_anim_set_time(&a, ANIM_MIN_MS);
    lv_anim_set_ready_cb(&a, NULL);
    lv_anim_start(&a);

    /* Hour hand: 0 -> target, 2000 ms — ready_cb on the slowest */
    lv_anim_set_exec_cb(&a, sweep_anim_hour_cb);
    lv_anim_set_var(&a, g_hand_hour);
    lv_anim_set_values(&a, 0, t_hour);
    lv_anim_set_time(&a, ANIM_HOUR_MS);
    lv_anim_set_ready_cb(&a, anim_ready_cb);
    lv_anim_start(&a);
}

/* ========================================================================
 * Create lock screen clock UI
 *
 * Creation order determines z-index:
 *   1. clock face image (backmost)
 *   2. date window (3 o'clock)
 *   3. three hands
 *   4. center axle dot
 *   5. status text
 * ======================================================================== */
void create_lock_screen_clock(void)
{
    if (g_lock_container != NULL)
        return;

    lv_obj_t* scr = lv_scr_act();
    /* Keep the screen background set by the CLOCK transition in
     * dashboard_timer_cb — either theme gradient + bg_image for
     * themes A/B (Cobalt/Inferno) or pure black for Silicon /
     * direct boot.  The transparent container below lets the
     * 24 px top/bottom strips blend seamlessly with whatever
     * background is set. */

    /* ---- Fully transparent 400 x 480 root container (centred) ---- */
    g_lock_container = lv_obj_create(scr);
    lv_obj_set_size(g_lock_container, CLOCK_AREA_W, CLOCK_AREA_H);
    lv_obj_set_pos(g_lock_container,
                   (SCREEN_WIDTH - CLOCK_AREA_W) / 2,
                   (SCREEN_HEIGHT - CLOCK_AREA_H) / 2);
    lv_obj_set_style_pad_all(g_lock_container, 0, 0);
    lv_obj_set_style_border_width(g_lock_container, 0, 0);
    lv_obj_set_style_radius(g_lock_container, 0, 0);
    lv_obj_set_style_bg_opa(g_lock_container, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(g_lock_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_lock_container, LV_OBJ_FLAG_HIDDEN);

    /* Hide persistent watermark during CLOCK mode */
    theme_watermark_show(false);

    /* ---- Pre-load clock image to PSRAM (one-time) ----
     *
     * The clock face's raw ARGB8888 pixel data is in SPI flash.  On this
     * platform the MPU may disable D-cache for flash-mapped regions,
     * making every renderer pixel-read go directly to the SPI bus.
     * The first full-frame render, which composites the entire 640 KB
     * image (400×400×4), stalls on ~2500 flash page-setup penalties —
     * producing partial / zero data for an upper-left region of the
     * clock face on the very first frame.
     *
     * Fix: copy the raw pixel data into a PSRAM buffer once, then
     * point the LVGL image descriptor at the PSRAM copy.  PSRAM is
     * D-cacheable on this platform, so every renderer read hits the
     * cache after the first cache-line fill — no flash stalls.
     *
     * NB: pvPortMalloc / malloc are avoided here because the linker
     * layout only manages the first 4 MB of PSRAM (0x60000000 –
     * 0x60400000 on this platform).  The framebuffers (2 × 1·5 MB),
     * LVGL heap, and network buffers already consume most of that
     * region, making a contiguous 640 KB block unlikely.  Instead we
     * place the copy at a fixed address in the upper (unmanaged)
     * PSRAM range — boot log confirms actual PSRAM goes to 0x61000000,
     * so 0x60500000 is safely above the layout limit.               */
    {
        const lv_image_dsc_t* clock_src = CLOCK_IMG_SRC;
        if (!s_img_cached)
        {
            /*
             * Place the copy at 0x60300000 — safely within the 4 MB
             * MPU-cached PSRAM region (0x60000000 – 0x603FFFFF).
             *
             * CRITICAL: the image bitmap must fit within the cacheable
             * range.  At 0x60300000 the data ends at 0x6039C400 (400×400×4)
             * or 0x603A8C00 (400×432×4), well below the 0x60400000 MPU
             * boundary.  Addresses ≥ 0x60400000 are Device / Strongly-
             * Ordered memory where LDM/STM multi-word loads used by
             * the LVGL SW renderer return incorrect pixel data.          */
            uint8_t* buf = (uint8_t*) 0x60300000u;
            memcpy(buf, clock_src->data, clock_src->data_size);
            DCache_Clean((uint32_t) buf, clock_src->data_size);
            __DSB();

            memcpy(&s_img_ram, clock_src, sizeof(lv_img_dsc_t));
            s_img_ram.data = (const uint8_t*) buf;
            s_img_cached   = true;

            RTK_LOGI("LOCK_SCREEN",
                     "clock image copied to PSRAM (%d bytes)\n",
                     (int) clock_src->data_size);
        }
        else
        {
            RTK_LOGI("LOCK_SCREEN",
                     "clock image reuse PSRAM cache\n");
        }

        /* ---- 1. Clock face (CLOCK_IMG_W x CLOCK_IMG_H) ---- */
        lv_obj_t* img = lv_image_create(g_lock_container);
        lv_image_set_src(img, &s_img_ram);
        lv_obj_set_size(img, CLOCK_IMG_W, CLOCK_IMG_H);
        lv_obj_set_pos(img, LOCAL_CENTER_X - CLOCK_IMG_W / 2, LOCAL_CENTER_Y - CLOCK_IMG_H / 2);
    }

    /* ---- 2. Date window at 3 o'clock (split-colour + 3D bevel, BELOW hands) ---- */
    {
        lv_obj_t* dw = lv_obj_create(g_lock_container);
        lv_obj_set_size(dw, 70, 26);
        lv_obj_set_style_border_width(dw, 0, 0);
        lv_obj_set_style_radius(dw, 6, 0);
        lv_obj_set_style_pad_all(dw, 0, 0);
        lv_obj_set_style_bg_color(dw, lv_color_make(0x28, 0x38, 0x48), 0);
        lv_obj_set_style_bg_opa(dw, LV_OPA_COVER, 0);
        lv_obj_set_style_clip_corner(dw, true, 0);
        lv_obj_set_style_outline_color(dw, lv_color_make(0x00, 0x00, 0x00), 0);
        lv_obj_set_style_outline_opa(dw, 30, 0);
        lv_obj_set_style_outline_width(dw, 1, 0);
        lv_obj_set_style_outline_pad(dw, 0, 0);
        lv_obj_remove_flag(dw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(dw, LV_ALIGN_CENTER, CLOCK_RADIUS - 70, 0);
        g_date_window = dw;

        /* Left half — date, white -> warm-grey gradient */
        g_date_label = lv_label_create(dw);
        lv_label_set_text(g_date_label, "--");
        lv_obj_set_size(g_date_label, 28, 26);
        lv_obj_set_pos(g_date_label, 0, 0);
        lv_obj_set_style_bg_color(g_date_label, lv_color_make(0xF5, 0xF0, 0xE8), 0);
        lv_obj_set_style_bg_grad_color(g_date_label, lv_color_make(0xE8, 0xE0, 0xD0), 0);
        lv_obj_set_style_bg_grad_dir(g_date_label, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(g_date_label, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(g_date_label, lv_color_make(0x22, 0x33, 0x44), 0);
        lv_obj_set_style_text_font(g_date_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(g_date_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(g_date_label, 6, 0);

        /* Right half — weekday, mid-blue -> deeper-blue gradient */
        g_week_label = lv_label_create(dw);
        lv_label_set_text(g_week_label, "---");
        lv_obj_set_size(g_week_label, 42, 26);
        lv_obj_set_pos(g_week_label, 28, 0);
        lv_obj_set_style_bg_color(g_week_label, lv_color_make(0x33, 0x44, 0x55), 0);
        lv_obj_set_style_bg_grad_color(g_week_label, lv_color_make(0x28, 0x38, 0x48), 0);
        lv_obj_set_style_bg_grad_dir(g_week_label, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(g_week_label, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(g_week_label, lv_color_make(0xCC, 0xDD, 0xEE), 0);
        lv_obj_set_style_text_font(g_week_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(g_week_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(g_week_label, 6, 0);
    }

    /* ---- 3. Three hands as lv_line (ON TOP of date window) ---- */
    g_hand_hour = lv_line_create(g_lock_container);
    lv_line_set_points(g_hand_hour, g_pts_hour, 2);
    lv_obj_set_style_line_width(g_hand_hour, HAND_HOUR_W, 0);
    lv_obj_set_style_line_color(g_hand_hour, lv_color_make(0xFF, 0xFF, 0x00), 0);
    lv_obj_set_style_line_rounded(g_hand_hour, 1, 0);

    g_hand_min = lv_line_create(g_lock_container);
    lv_line_set_points(g_hand_min, g_pts_min, 2);
    lv_obj_set_style_line_width(g_hand_min, HAND_MIN_W, 0);
    lv_obj_set_style_line_color(g_hand_min, lv_color_make(0xE0, 0xE0, 0xE0), 0);
    lv_obj_set_style_line_rounded(g_hand_min, 1, 0);

    g_hand_sec = lv_line_create(g_lock_container);
    lv_line_set_points(g_hand_sec, g_pts_sec, 2);
    lv_obj_set_style_line_width(g_hand_sec, HAND_SEC_W, 0);
    lv_obj_set_style_line_color(g_hand_sec, lv_color_make(0xFF, 0x20, 0x20), 0);

    /* ---- 4. Center axle dot ---- */
    lv_obj_t* dot = lv_obj_create(g_lock_container);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_pos(dot, LOCAL_CENTER_X - 7, LOCAL_CENTER_Y - 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_make(0x22, 0x33, 0x44), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_style_border_color(dot, lv_color_make(0x44, 0x66, 0x88), 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 5. Status text below clock area ---- */
    lv_obj_t* status = lv_label_create(g_lock_container);
    lv_label_set_text(status, "PC LOCKED - Standby Mode");
    lv_obj_set_style_text_color(status, lv_color_make(0x55, 0x66, 0x77), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, CLOCK_AREA_H / 2 - 10);

    /* Backlight already dimmed by standby_enter() — no redundant call here. */

    /* Show container now (fully built) */
    lv_obj_remove_flag(g_lock_container, LV_OBJ_FLAG_HIDDEN);

    /* Enforce correct z-order:
     *   clock face -> date window -> hands -> center dot */
    lv_obj_move_foreground(g_hand_hour);
    lv_obj_move_foreground(g_hand_min);
    lv_obj_move_foreground(g_hand_sec);
    lv_obj_move_foreground(dot);

    /* Pre-warm: sequential 32-bit reads through D-cache so the
     * first lv_refr_now() renderer encounters hot cache lines for
     * the entire PSRAM-based clock face bitmap.                       */
    {
        const volatile uint32_t* p                            = (const volatile uint32_t*) s_img_ram.data;
        uint32_t                 word_count                   = s_img_ram.data_size / 4;
        volatile uint32_t        warm __attribute__((unused)) = 0;
        for (uint32_t i = 0; i < word_count; i++)
            warm += p[i];
    }

    /* Synchronous double-buffer fill.
     *
     * Pass 1 — full-screen invalidate establishes the screen background
     * (gradient or black) and the initial clock scene in one buffer.
     * Pass 2 — full-screen invalidate again so the OTHER buffer also
     * receives the complete composited scene.  Previously pass 2 only
     * invalidated the container, but the flush log showed that the
     * container area wasn't being picked up — only a tiny bottom-right
     * rect was dirty, leaving the rest of buffer B with stale data. */
    {
        lv_display_t* _disp = lv_display_get_default();
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(_disp);
        __DSB();
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(_disp);
    }

    /* Reset tracking */
    g_lock_last_second = 0;
    g_last_day         = -1;
    g_animating        = false;
    g_sweep_done       = false;

    /* ---- Try to get time and start sweep animation ---- */
    taskENTER_CRITICAL();
    uint32_t ts_base    = g_time_base_ts;
    uint32_t ts_base_ms = g_time_base_ms;
    taskEXIT_CRITICAL();

    if (ts_base == 0 || ts_base < 1700000000)
    {
        /* Try SNTP as fallback */
        time_t sntp_now = time(NULL);
        if (sntp_now > 1700000000)
        {
            ts_base    = (uint32_t) sntp_now;
            ts_base_ms = rtos_time_get_current_system_time_ms();
            taskENTER_CRITICAL();
            g_time_base_ts = ts_base;
            g_time_base_ms = ts_base_ms;
            taskEXIT_CRITICAL();
        }
    }

    if (ts_base != 0 && ts_base > 1700000000)
    {
        uint32_t now_ms     = rtos_time_get_current_system_time_ms();
        uint32_t elapsed_s  = (now_ms - ts_base_ms) / 1000;
        uint32_t current_ts = ts_base + elapsed_s + UTC8_OFFSET_SEC;

        uint16_t yr;
        uint8_t  mo, da, hr, mi, se;
        unix_to_datetime(current_ts, &yr, &mo, &da, &hr, &mi, &se);

        /* Block 1Hz updater during sweep animation */
        g_lock_last_second = current_ts;

        /* Initial date label */
        {
            static const char* dow[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            int                wd    = calc_weekday((int) yr, (int) mo, (int) da);
            char               d_buf[8];
            char               w_buf[8];
            snprintf(d_buf, sizeof(d_buf), "%02u", (unsigned) da);
            snprintf(w_buf, sizeof(w_buf), "%s", dow[wd]);
            lv_label_set_text(g_date_label, d_buf);
            lv_label_set_text(g_week_label, w_buf);
        }
        g_last_day = (int) da;

        /* Defer sweep start — store angles, start from update() */
        int t_sec       = (int) se * 6;
        int t_min       = (int) mi * 6;
        int t_hour      = (int) (hr % 12) * 30 + (int) (mi / 2);
        g_sweep_pending = true;
        g_pend_sec      = t_sec;
        g_pend_min      = t_min;
        g_pend_hour     = t_hour;
    }
}

/* ========================================================================
 * Periodic 1 Hz clock update.
 *
 * The container is permanently transparent — the initial double
 * lv_refr_now() baked the composited scene into both DIRECT
 * framebuffers once and never needs to re-render static content.
 *
 * lv_line_set_points() dirties only the hand bounding box (roughly
 * HAND_SEC_LEN x 2 pixels).  A pair of lv_refr_now() calls renders
 * that small region into both DIRECT framebuffers synchronously,
 * eliminating the need for full-screen or container-wide redraws.
 *
 * If the first valid time arrives late (SNTP hadn't synced during
 * create), start the sweep animation here instead of directly setting
 * the hands.
 * ======================================================================== */
void update_lock_screen_clock(void)
{
    if (g_lock_container == NULL)
        return;

    /* Deferred sweep: start on first update after create */
    if (g_sweep_pending)
    {
        g_sweep_pending = false;
        start_sweep_animation(g_pend_sec, g_pend_min, g_pend_hour);
        return;
    }

    /* Block hand update while the initial sweep animation is running */
    if (g_animating)
        return;

    taskENTER_CRITICAL();
    uint32_t ts_base    = g_time_base_ts;
    uint32_t ts_base_ms = g_time_base_ms;
    taskEXIT_CRITICAL();

    if (ts_base == 0 || ts_base < 1700000000)
    {
        /* Try SNTP as fallback */
        time_t sntp_now = time(NULL);
        if (sntp_now > 1700000000)
        {
            ts_base    = (uint32_t) sntp_now;
            ts_base_ms = rtos_time_get_current_system_time_ms();
            taskENTER_CRITICAL();
            g_time_base_ts = ts_base;
            g_time_base_ms = ts_base_ms;
            taskEXIT_CRITICAL();
        }
        else
        {
            return;
        }
    }

    uint32_t now_ms     = rtos_time_get_current_system_time_ms();
    uint32_t elapsed_s  = (now_ms - ts_base_ms) / 1000;
    uint32_t current_ts = ts_base + elapsed_s + UTC8_OFFSET_SEC;

    /* If this is the first valid time and sweep hasn't been done,
     * start the sweep animation now instead of directly setting hands. */
    if (!g_sweep_done)
    {
        uint16_t yr;
        uint8_t  mo, da, hr, mi, se;
        unix_to_datetime(current_ts, &yr, &mo, &da, &hr, &mi, &se);

        int t_sec  = (int) se * 6;
        int t_min  = (int) mi * 6;
        int t_hour = (int) (hr % 12) * 30 + (int) (mi / 2);

        g_lock_last_second = current_ts;

        /* Set initial date */
        {
            static const char* dow[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
            int                wd    = calc_weekday((int) yr, (int) mo, (int) da);
            char               d_buf[8];
            char               w_buf[8];
            snprintf(d_buf, sizeof(d_buf), "%02u", (unsigned) da);
            snprintf(w_buf, sizeof(w_buf), "%s", dow[wd]);
            lv_label_set_text(g_date_label, d_buf);
            lv_label_set_text(g_week_label, w_buf);
        }
        g_last_day = (int) da;

        start_sweep_animation(t_sec, t_min, t_hour);
        return;
    }

    /* Dedup: skip hand update if same second (1 Hz time resolution) */
    if (current_ts == g_lock_last_second)
        return;
    g_lock_last_second = current_ts;

    uint16_t yr;
    uint8_t  mo, da, hr, mi, se;
    unix_to_datetime(current_ts, &yr, &mo, &da, &hr, &mi, &se);

    int t_sec  = (int) se * 6;
    int t_min  = (int) mi * 6;
    int t_hour = (int) (hr % 12) * 30 + (int) (mi / 2);

    set_hand_angle(g_pts_sec, HAND_SEC_LEN, t_sec);
    lv_line_set_points(g_hand_sec, g_pts_sec, 2);
    set_hand_angle(g_pts_min, HAND_MIN_LEN, t_min);
    lv_line_set_points(g_hand_min, g_pts_min, 2);
    set_hand_angle(g_pts_hour, HAND_HOUR_LEN, t_hour);
    lv_line_set_points(g_hand_hour, g_pts_hour, 2);

    /* Update date at midnight boundary */
    if ((int) da != g_last_day)
    {
        static const char* dow[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        int                wd    = calc_weekday((int) yr, (int) mo, (int) da);
        char               d_buf[8];
        char               w_buf[8];
        snprintf(d_buf, sizeof(d_buf), "%02u", (unsigned) da);
        snprintf(w_buf, sizeof(w_buf), "%s", dow[wd]);
        lv_label_set_text(g_date_label, d_buf);
        lv_label_set_text(g_week_label, w_buf);
        g_last_day = (int) da;
    }

    /*
     * Pre-warm the clock face bitmap in data cache from the PSRAM copy,
     * NOT from the original flash source.  The 2-4 second
     * sweep animation constantly reads clock face pixels for hand bounding-
     * box compositing; by the time the animation finishes the image data
     * has typically been evicted from the ~32 KB L1 cache.  Without a
     * forced warm-up the first lv_refr_now() would encounter cold cache
     * lines, causing the clock face to render partially transparent for
     * exactly one frame — visible as the theme background flashing through.
     *
     * NB: must read from s_img_ram.data (PSRAM, D-cacheable) — the
     * original image data is in SPI flash, mapped uncacheable on this
     * platform, so reading from flash misses the D-cache entirely.       */
    {
        const volatile uint8_t* p                            = (const volatile uint8_t*) s_img_ram.data;
        uint32_t                sz                           = s_img_ram.data_size;
        volatile uint8_t        warm __attribute__((unused)) = 0;
        for (uint32_t i = 0; i < sz; i += 32)
            warm += p[i];
    }

    /*
     * Single-pass synchronous render.
     *
     * lv_line_set_points() above has already dirtied the hand area.
     * lv_refr_now() renders the dirty region synchronously so the
     * hand update appears in this tick (not delayed by one frame).
     *
     * flush_commit is NOT called here — it runs from the main loop
     * (app_main.c) via lcdc_core_flush_commit() after lv_timer_handler
     * returns, when ALL timer callbacks and _lv_refr_task have finished.
     * Calling it here (inside a timer callback inside lv_timer_handler)
     * would commit a half-baked buffer if _lv_refr_task later renders
     * additional dirty areas created by other timer callbacks.
     *
     * The main-loop frame gate (while lcdc_core_is_flip_pending())
     * ensures the previous frame's pending flip is consumed by LINE ISR
     * before we start a new frame, preventing flush_commit from
     * overwriting an unconsumed pending flip (which would drop a frame).
     */
    {
        lv_display_t* _disp = lv_display_get_default();
        lv_refr_now(_disp);
    }
}

/* ========================================================================
 * Unlock transition: fade-in monitor layout (already on top of clock).
 *
 * The monitor layout was created by layout_switch() AFTER the clock
 * container, so in LVGL's z-order it sits ABOVE the clock naturally.
 * Instead of moving the clock to front (which flashes one frame of
 * full-opa clock), we keep the clock underneath and fade the monitor
 * in from transparent → opaque.  On completion the clock is destroyed.
 * ======================================================================== */

static void unlock_fade_cb(void* var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t*) var, (lv_opa_t) v, 0);
}

static void unlock_fade_ready_cb(lv_anim_t* a)
{
    LV_UNUSED(a);
    /* Restore full brightness on unlock */
    backlight_set_standby(false);

    /* Destroy the clock UI */
    if (g_lock_container != NULL)
    {
        lv_obj_delete(g_lock_container);
        g_lock_container = NULL;
    }

    g_hand_hour   = NULL;
    g_hand_min    = NULL;
    g_hand_sec    = NULL;
    g_date_window = NULL;
    g_date_label  = NULL;
    g_week_label  = NULL;
    g_animating   = false;

    /* Reset hand points to 12 o'clock so the next create starts clean */
    g_pts_sec[1].x  = LOCAL_CENTER_X;
    g_pts_sec[1].y  = LOCAL_CENTER_Y - HAND_SEC_LEN;
    g_pts_min[1].x  = LOCAL_CENTER_X;
    g_pts_min[1].y  = LOCAL_CENTER_Y - HAND_MIN_LEN;
    g_pts_hour[1].x = LOCAL_CENTER_X;
    g_pts_hour[1].y = LOCAL_CENTER_Y - HAND_HOUR_LEN;

    /* Restore watermark (hidden during CLOCK mode) */
    theme_watermark_show(true);
}

void start_unlock_transition(void)
{
    if (g_lock_container == NULL)
        return;

    /* Immediately mark as inactive so the 1 Hz timer won't re-trigger */
    g_lock_screen_active = false;

    /* The monitor layout (already above clock in z-order): fade it in from
     * transparent → opaque so the clock beneath it appears to dissolve away.
     * No move_foreground needed — layout_switch creates on scr_act() AFTER
     * the clock, so the layout is naturally on top. */
    lv_obj_t* monitor = layout_get_container();
    if (monitor)
    {
        lv_obj_set_style_opa(monitor, LV_OPA_TRANSP, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, monitor);
        lv_anim_set_exec_cb(&a, unlock_fade_cb);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_ready_cb(&a, unlock_fade_ready_cb);
        lv_anim_start(&a);
    }
    else
    {
        /* No monitor layout — just destroy clock directly */
        unlock_fade_ready_cb(NULL);
    }
}

/* ========================================================================
 * Destroy lock screen clock UI
 * ======================================================================== */
void destroy_lock_screen_clock(void)
{
    /* Restore full brightness before destroying */
    backlight_set_standby(false);

    g_animating = false; /* allow subsequent create to run cleanly */

    if (g_lock_container != NULL)
    {
        lv_obj_delete(g_lock_container);
        g_lock_container = NULL;
    }

    g_hand_hour = NULL;
    g_hand_min  = NULL;
    g_hand_sec  = NULL;

    /* Reset hand points to 12 o'clock so the next create starts clean */
    g_pts_sec[1].x  = LOCAL_CENTER_X;
    g_pts_sec[1].y  = LOCAL_CENTER_Y - HAND_SEC_LEN;
    g_pts_min[1].x  = LOCAL_CENTER_X;
    g_pts_min[1].y  = LOCAL_CENTER_Y - HAND_MIN_LEN;
    g_pts_hour[1].x = LOCAL_CENTER_X;
    g_pts_hour[1].y = LOCAL_CENTER_Y - HAND_HOUR_LEN;

    /* Restore watermark */
    theme_watermark_show(true);
    g_date_window = NULL;
    g_date_label  = NULL;
    g_week_label  = NULL;
}
