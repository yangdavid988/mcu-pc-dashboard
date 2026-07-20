#include "pc_dashboard.h"
#include "pc_dashboard_ui.h"
#include "pc_dashboard_layout.h"
#include "threshold_config.h"
#include "gpio_control.h"
#include "WiFi_reconnect.h"
/* PPE hardware-accelerated draw unit (SDK AmebaGreen2 AG2 driver) */
//#include "lv_draw_ppe.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ameba_pmu.h"      /* pmu_acquire_wakelock */
#include "drv/lcd/lcdc_core.h"   /* lcdc_core_flush_commit */

/* PMU device ID for LCDC — hold wakelock during LVGL operation to prevent
 * tickless idle from gating LCDC clock (LCDC is SOC domain, cannot wake CPU). */
#define PMU_LCDC_DEVICE      PMU_OS

#ifndef TAG
#define TAG     "APP_MAIN"
#endif

 /* ========================================================================
  * Fast flash — driven by LVGL timer, aligned with render cycle
  * ======================================================================== */
  /* FAST_FLASH_MS now comes from threshold_config.h: g_flash_threshold.flash_interval_ms */

  /* LVGL thread parameters */
#ifndef TASK_STACK_LVGL
#define TASK_STACK_LVGL         8192
#endif
#ifndef TASK_PRIO_LVGL
#define TASK_PRIO_LVGL          (tskIDLE_PRIORITY + 3)
#endif

/* WiFi connection thread parameters */
#ifndef TASK_STACK_WIFI
#define TASK_STACK_WIFI         2048
#endif
#ifndef TASK_PRIO_WIFI
#define TASK_PRIO_WIFI          (tskIDLE_PRIORITY)
#endif

/* ========================================================================
 * Fast flash LVGL timer callback
 * ======================================================================== */
static void flash_timer_cb(lv_timer_t* timer)
{
    LV_UNUSED(timer);
    fast_flash_tick();
}

/* LVGL display buffers (dual-buffer via lcd_get_fb_base) */
static u8* lv_disp_buf1 = NULL;
static u8* lv_disp_buf2 = NULL;

/* Screen dimensions */
uint16_t lcd_w = SCREEN_WIDTH;
uint16_t lcd_h = SCREEN_HEIGHT;

/* ========================================================================
 * LVGL main thread (initialization + render loop)
 * ======================================================================== */
static void lvgl_main_thread(void* parameters)
{
    (void)parameters;

    RTK_LOGS(TAG, RTK_LOG_INFO,
        "\r\n=== PC Dashboard ===\r\n");

    /* LCD initialization */
    lcd_init();

    /* Hold PMU wakelock for entire LVGL runtime — prevents tickless idle from
     * entering sleep modes that gate LCDC clock. LCDC (SOC domain) cannot wake CPU,
     * so any sleep during a flip window would cause permanent freeze. */
    pmu_acquire_wakelock(PMU_LCDC_DEVICE);

    /* Get framebuffer base addresses (auto-selected by screen type, dual-buffer) */
    {
        uint32_t fb1, fb2;
        lcd_get_fb_base(&fb1, &fb2);
        lv_disp_buf1 = (u8*)fb1;
        lv_disp_buf2 = (u8*)fb2;
        RTK_LOGI(TAG, "FB base1=0x%08lX base2=0x%08lX driver=%s\n",
            (unsigned long)fb1, (unsigned long)fb2, lcd_get_driver_name());
    }

    /* GPIO button initialization */
    gpio_control_init();
    RTK_LOGI(TAG, "GPIO buttons initialized\n");

    /* LVGL initialization */
    lv_init();
    lv_tick_set_cb(custom_tick_get);

    /* Register PPE hardware-accelerated Draw Unit (must be before lv_display_create) */
    // lv_draw_ppe_init();

    lv_display_t* display = lv_display_create(lcd_w, lcd_h);
    lv_display_set_flush_cb(display, lvgl_disp_flush);
    lv_display_set_buffers(display,
        lv_disp_buf1,
        lv_disp_buf2,           /* Dual-buffer with VBlank page flip to eliminate tearing */
        LVGL_BUF_SIZE,
        LV_DISPLAY_RENDER_MODE_DIRECT);

    /* Create dashboard UI */
    create_dashboard_ui();

    /* Start UI update timer (1s interval) */
    g_dashboard_timer = lv_timer_create(dashboard_timer_cb, UI_UPDATE_INTERVAL_MS, NULL);

    /* Start fast flash timer (aligned with LVGL render cycle) */
    lv_timer_create(flash_timer_cb, g_flash_threshold.flash_interval_ms, NULL);

    RTK_LOGI(TAG, "LVGL UI ready, starting main loop...\n");

    /* LVGL main loop */
    {
        uint32_t diag_lvgl_tick = 0;
        uint32_t diag_lvgl_cnt = 0;
        while (1)
        {
            uint32_t _now = rtos_time_get_current_system_time_ms();
            if (_now - diag_lvgl_tick >= 5000)
            {
                diag_lvgl_tick = _now;
#if defined(CONFIG_DIAG_HEARTBEAT)
                RTK_LOGI(TAG, "DIAG: lvgl cnt=%lu\n", (unsigned long)diag_lvgl_cnt);
#endif
            }
            diag_lvgl_cnt++;

            uint32_t time_till_next = lv_timer_handler();
            if (time_till_next == LV_NO_TIMER_READY)
                time_till_next = LV_DEF_REFR_PERIOD;
            rtos_time_delay_ms(time_till_next);
        }
    }

    rtos_task_delete(NULL);
}

/* ========================================================================
 * Application entry point
 * ======================================================================== */
void app_example(void)
{
    RTK_LOGI(TAG, "PC Dashboard started!\r\n");

    /* Create LVGL UI thread (high priority for smooth refresh) */
    if (rtos_task_create(NULL,
        "lvgl_thread",
        (rtos_task_t)lvgl_main_thread,
        NULL,
        TASK_STACK_LVGL,
        TASK_PRIO_LVGL) != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "Create LVGL thread failed!\r\n");
        return;
    }

    /* Create WiFi connection task (low priority, exits after connecting) */
    if (rtos_task_create(NULL,
        "WiFi_connect",
        (rtos_task_t)WiFi_connect_task,
        NULL,
        TASK_STACK_WIFI,
        TASK_PRIO_WIFI) != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "Create WiFi connect task failed!\r\n");
    }

    /* Start PC Dashboard main task (MQTT subscribe + data processing) */
    pc_dashboard_start();

    RTK_LOGI(TAG, "All tasks created.\r\n");
}
