/* ========================================================================
 * USB CDC ACM receiver — PC stats via USB virtual serial port
 *
 * Guard: only compiled when CONFIG_USB_CDC_MODE is defined (ST7262 only).
 * DBL070 is excluded via Kconfig dependency (depends on SCREEN_ST7262).
 *
 * NOTE: platform_autoconf.h must be included BEFORE the #ifdef guard
 * to break the circular dependency: the header provides CONFIG_USB_CDC_MODE
 * (#define), and the guard gates the rest of the file.
 * ======================================================================== */

#include <platform_autoconf.h>

#ifdef CONFIG_USB_CDC_MODE

#include "usb_cdc_receiver.h"
#include <basic_types.h>
#include "usbd_cdc_acm.h"
#include "os_wrapper.h"
#include "log.h"
#include "cJSON.h"

#include "core/pc_dashboard.h"   /* g_pc_stats, g_new_data_ready, PC_Stats_t  */
#include "core/standby_manager.h" /* standby_enter, standby_exit               */
#include "config/sdk_compat.h"    /* COMPAT_CHECK_CONNECTIVITY (unused here)   */

/* ========================================================================
 * USB CDC ACM receiver — PC stats via USB virtual serial port
 *
 * Architecture:
 *
 *   PC (USB Host)  --BULK OUT-->  cdc_acm_cb_received() [ISR]
 *                                      |
 *                                   ring buffer (16 KB)
 *                                      |
 *                                   rtos_sema_give()
 *                                      |
 *                              usb_cdc_rx_task() [worker thread]
 *                                      |
 *                              line framing (detect '\n')
 *                                     / \
 *                           event JSON   stats JSON
 *                              |              |
 *                         standby_enter/   parse_pc_stats_json()
 *                         standby_exit()
 *
 * The received() callback runs in ISR context and must not call any
 * blocking or heap-allocating function. Data is pushed into a lock-free
 * single-producer single-consumer ring buffer and processed by the
 * worker thread.
 *
 * Line protocol:
 *   Each JSON message is terminated by a single '\n' (LF) byte, as
 *   produced by pc_to_usb.py's json.dumps() + '\n'.  Lines are
 *   assembled in a dedicated line buffer; a complete line triggers
 *   JSON dispatch.
 * ======================================================================== */

/* Private defines ---------------------------------------------------------*/

/* USB speed: prefer High-Speed; fall back to Full-Speed on FS-only SoCs */
#ifdef CONFIG_SUPPORT_USB_FS_ONLY
#define USB_CDC_SPEED               USB_SPEED_FULL
#else
#define USB_CDC_SPEED               USB_SPEED_HIGH
#endif

/* Transfer sizes (match HW BULK endpoint max packet) */
#define USB_CDC_BULK_OUT_XFER_SIZE  2048U
#define USB_CDC_BULK_IN_XFER_SIZE   2048U

/* Ring buffer: 16 KB (SPSC, power-of-two for wrap masking) */
#define USB_CDC_RING_BUF_SIZE       (16 * 1024)
#define USB_CDC_RING_MASK           (USB_CDC_RING_BUF_SIZE - 1)

/* Maximum single JSON line length (12 KB covers LHM full output) */
#define USB_CDC_LINE_BUF_SIZE       (12 * 1024)

/* Thread parameters */
#define USB_CDC_TASK_STACK_SIZE     2048U
#define USB_CDC_TASK_PRIORITY       (tskIDLE_PRIORITY + 2)

/* Log tag */
#ifndef TAG
#define TAG "USB_CDC"
#endif

/* Private variables -------------------------------------------------------*/

/* Ring buffer state (volatile because ISR writes head, task reads tail) */
static volatile u32  usb_cdc_ring_head;
static volatile u32  usb_cdc_ring_tail;
static u8            usb_cdc_ring[USB_CDC_RING_BUF_SIZE] __attribute__((aligned(32)));

/* Line assembly buffer */
static u8            usb_cdc_line_buf[USB_CDC_LINE_BUF_SIZE];
static u32           usb_cdc_line_pos;

/* Synchronisation semaphore: ISR gives, worker task takes */
static rtos_sema_t   usb_cdc_rx_sema;

/* USB configuration */
static const usbd_config_t usb_cdc_cfg = {
    .speed        = USB_CDC_SPEED,
    .isr_priority = INT_PRI_MIDDLE,
#if defined(CONFIG_AMEBAGREEN2)
    .rx_fifo_depth    = 644U,
    .ptx_fifo_depth   = {16U, 256U, 32U, 16U, 16U, },
#elif defined(CONFIG_AMEBAL2)
    .rx_fifo_depth    = 661U,
    .ptx_fifo_depth   = {256U, 16U, 32U, 16U, },
#elif defined(CONFIG_AMEBAPRO3)
    .rx_fifo_depth    = 1664U,
    .ptx_fifo_depth   = {256U, 32U, 16U, },
#endif
};

/* Private function prototypes ---------------------------------------------*/

static int  usb_cdc_cb_init(void);
static int  usb_cdc_cb_deinit(void);
static int  usb_cdc_cb_setup(usb_setup_req_t *req, u8 *buf);
static int  usb_cdc_cb_received(u8 *buf, u32 len);
static void usb_cdc_cb_status_changed(u8 old_status, u8 status);

static void usb_cdc_rx_task(void *param);

/* Ring buffer helpers */
static u32  ring_available(void);
static void ring_write(const u8 *data, u32 len);
static u8   ring_read_byte(void);

/* Line dispatch */
static void dispatch_line(u8 *line, u32 len);

/* ---------------------------------------------------------------------------
 * USB CDC ACM callbacks (all in ISR context)
 * --------------------------------------------------------------------------- */

static const usbd_cdc_acm_cb_t usb_cdc_cb = {
    .init            = usb_cdc_cb_init,
    .deinit          = usb_cdc_cb_deinit,
    .setup           = usb_cdc_cb_setup,
    .received        = usb_cdc_cb_received,
    .status_changed  = usb_cdc_cb_status_changed,
};

static int usb_cdc_cb_init(void)
{
    RTK_LOGI(TAG, "CDC ACM init callback\n");
    return HAL_OK;
}

static int usb_cdc_cb_deinit(void)
{
    RTK_LOGI(TAG, "CDC ACM deinit callback\n");
    return HAL_OK;
}

static int usb_cdc_cb_setup(usb_setup_req_t *req, u8 *buf)
{
    /* Handle essential CDC control requests */
    switch (req->bRequest)
    {
    case USB_CDC_ACM_SET_LINE_CODING:
        /* Accept host-set baud/format — we don't actually care */
        break;
    case USB_CDC_ACM_GET_LINE_CODING:
        /* Return current line coding (defaults are fine) */
        buf[0] = 0xC0; buf[1] = 0x84; buf[2] = 0x01; buf[3] = 0x00; /* 115200 */
        buf[4] = 0x00; /* 1 stop bit */
        buf[5] = 0x00; /* no parity */
        buf[6] = 0x08; /* 8 data bits */
        break;
    case USB_CDC_ACM_SET_CONTROL_LINE_STATE:
        /* DTR/RTS handshake — log activation for debug */
        if (req->wValue & 0x01)
        {
            USB_DIAG(USB_LAYER_APP, USB_EVT_LINK, 0);
            RTK_LOGI(TAG, "Host DTR asserted (VCOM active)\n");
        }
        break;
    default:
        break;
    }

    return HAL_OK;
}

/**
 * @brief  Data received from PC host on BULK OUT endpoint.
 * @note   ISR context — NO blocking, NO malloc, NO cJSON parsing.
 *         Pushes bytes into the ring buffer and signals the worker task.
 */
static int usb_cdc_cb_received(u8 *buf, u32 len)
{
    if (len == 0)
        return HAL_OK;

    ring_write(buf, len);

    /* Wake the worker task to consume the data */
    if (usb_cdc_rx_sema)
    {
        rtos_sema_give(usb_cdc_rx_sema);
    }

    return HAL_OK;
}

static void usb_cdc_cb_status_changed(u8 old_status, u8 status)
{
    UNUSED(old_status);

    if (status == USBD_ATTACH_STATUS_ATTACHED)
    {
        RTK_LOGI(TAG, "USB attached (host enumerated)\n");
        /* USB CDC mode: no MQTT retained pc/event message.  Signal that a
         * host is connected so dashboard_timer_cb() does not delay layout
         * creation waiting for a non-existent retained message (15s stall). */
        g_pc_event_received = true;
    }
    else if (status == USBD_ATTACH_STATUS_DETACHED)
    {
        RTK_LOGI(TAG, "USB detached\n");
    }
}

/* ---------------------------------------------------------------------------
 * Lock-free single-producer (ISR) single-consumer (task) ring buffer
 *
 * head: written by ISR  (producer)
 * tail: read by task    (consumer)
 *
 * The buffer is full  when (head - tail) == USB_CDC_RING_BUF_SIZE.
 * The buffer is empty when head == tail.
 * Power-of-two size allows wrap via mask instead of modulo.
 * --------------------------------------------------------------------------- */

static u32 ring_available(void)
{
    return (usb_cdc_ring_head - usb_cdc_ring_tail);
}

static void ring_write(const u8 *data, u32 len)
{
    u32 head = usb_cdc_ring_head;
    u32 tail = usb_cdc_ring_tail;
    u32 i;

    for (i = 0; i < len; i++)
    {
        /* Drop data if ring is full — oldest data is preserved */
        if ((head - tail) >= USB_CDC_RING_BUF_SIZE)
            break;

        usb_cdc_ring[head & USB_CDC_RING_MASK] = data[i];
        head++;
    }

    usb_cdc_ring_head = head;
}

static u8 ring_read_byte(void)
{
    u8 byte;
    u32 tail = usb_cdc_ring_tail;

    if (usb_cdc_ring_head == tail)
        return 0; /* empty */

    byte = usb_cdc_ring[tail & USB_CDC_RING_MASK];
    tail++;
    usb_cdc_ring_tail = tail;

    return byte;
}

/* ---------------------------------------------------------------------------
 * Line dispatch
 *
 * Determine message type by inspecting the first bytes:
 *   {"event": ...}  →  lock / unlock
 *   other JSON      →  parse_pc_stats_json()
 * --------------------------------------------------------------------------- */

static void dispatch_line(u8 *line, u32 len)
{
    /* Guard: line must be at least 2 bytes */
    if (len < 2)
        return;

    /* Trim trailing whitespace / CR from line end (LF already consumed) */
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
    {
        len--;
    }
    if (len == 0)
        return;

    /* Null-terminate for cJSON / string functions.
     * line_buf has spare capacity because max line < buf size. */
    line[len] = '\0';

    /* Cheap event check: starts with '{"event"' or '{"event":' */
    if ((len >= 9 && memcmp(line, "{\"event\"", 8) == 0) ||
        (len >= 10 && memcmp(line, "{\"event\":", 9) == 0))
    {
        cJSON *root = cJSON_Parse((const char *)line);
        if (!root)
            return;

        cJSON *item = cJSON_GetObjectItem(root, "event");
        if (cJSON_IsString(item) && item->valuestring)
        {
            bool is_lock = (strcmp(item->valuestring, "lock") == 0);
            bool is_unlock = (strcmp(item->valuestring, "unlock") == 0);
            bool is_disconnect = (strcmp(item->valuestring, "disconnect") == 0);

            if (is_lock)
            {
                RTK_LOGI(TAG, "Lock event from USB\n");
                standby_enter();
            }
            else if (is_unlock)
            {
                RTK_LOGI(TAG, "Unlock event from USB\n");
                standby_exit();
            }

            else if (is_disconnect)
            {
                RTK_LOGI(TAG, "Disconnect event from USB\n");
                uint32_t now = rtos_time_get_current_system_time_ms();
                g_data_last_tick = now - CONNECTION_TIMEOUT_MS - 1000;
                pc_stats_reset_to_default();
            }

            /* Extract timestamp from event to set time base (no SNTP in USB CDC mode).
             * This ensures the clock displays correct time even if only lock/unlock
             * events arrive before any stats data (e.g. PC is locked at boot).      */
            if (is_lock || is_unlock)
            {
                cJSON *ts_item = cJSON_GetObjectItem(root, "timestamp");
                if (cJSON_IsNumber(ts_item))
                {
                    uint32_t ts = (uint32_t) ts_item->valuedouble;
                    if (ts > 1700000000) /* Sanity: must be post-2024 */
                    {
                        taskENTER_CRITICAL();
                        if (g_time_base_ts == 0)
                        {
                            g_time_base_ts = ts;
                            g_time_base_ms = rtos_time_get_current_system_time_ms();
                            RTK_LOGI(TAG, "Time base set from event ts=%u\n",
                                     (unsigned int) ts);
                        }
                        taskEXIT_CRITICAL();
                    }
                }
            }
        }

        cJSON_Delete(root);
    }
    else
    {
        /* PC stats or weather — parse_pc_stats_json silently skips
         * unknown fields (e.g. weather_*, lhm) so it handles both
         * bare stats and the augmented USB JSON.                    */
        parse_pc_stats_json((const char *)line);

        /* Receiving stats over USB implies PC is actively sending data
         * (pc_to_usb.py only sends stats when not locked).  If a stale
         * MQTT retained lock event put us in standby, exit it now.    */
        if (standby_is_active())
        {
            RTK_LOGI(TAG, "Stats via USB while in standby — exiting\n");
            standby_exit();
        }
    }
}

/* ---------------------------------------------------------------------------
 * Worker task
 * --------------------------------------------------------------------------- */

static void usb_cdc_rx_task(void *param)
{
    int  ret;
    u8   byte;
    u32  line_pos;

    UNUSED(param);

    RTK_LOGI(TAG, "Starting USB CDC ACM receiver...\n");

    /* Initialise synchronisation semaphore (binary, initial 0) */
    ret = rtos_sema_create(&usb_cdc_rx_sema, 0, 1);
    if (ret != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "Failed to create semaphore\n");
        goto exit_task;
    }

    /* Initialise USB device stack */
    ret = usbd_init(&usb_cdc_cfg);
    if (ret != HAL_OK)
    {
        RTK_LOGE(TAG, "usbd_init failed: %d\n", ret);
        goto exit_sema;
    }

    /* Initialise CDC ACM class */
    ret = usbd_cdc_acm_init(USB_CDC_BULK_OUT_XFER_SIZE,
                            USB_CDC_BULK_IN_XFER_SIZE,
                            &usb_cdc_cb);
    if (ret != HAL_OK)
    {
        RTK_LOGE(TAG, "usbd_cdc_acm_init failed: %d\n", ret);
        goto exit_usbd;
    }

    /* Reset line assembly state */
    usb_cdc_line_pos = 0;

    RTK_LOGI(TAG, "USB CDC ACM ready (waiting for host...)\n");

    /* ---------- Main receive loop ---------- */
    for (;;)
    {
        /* Wait for data from ISR */
        ret = rtos_sema_take(usb_cdc_rx_sema, RTOS_SEMA_MAX_COUNT);
        if (ret != RTK_SUCCESS)
            continue;

        /* Drain all currently available bytes */
        line_pos = usb_cdc_line_pos;

        while (ring_available() > 0)
        {
            byte = ring_read_byte();

            if (byte == '\n')
            {
                /* Complete line received — dispatch */
                if (line_pos > 0)
                {
                    dispatch_line(usb_cdc_line_buf, line_pos);
                }
                line_pos = 0;
            }
            else
            {
                /* Append to line buffer (safety: leave 1 byte for NUL) */
                if (line_pos < (USB_CDC_LINE_BUF_SIZE - 1))
                {
                    usb_cdc_line_buf[line_pos++] = byte;
                }
                else
                {
                    /* Line too long: reset to avoid data corruption.
                     * This should never happen with a properly configured
                     * line buffer (12 KB covers max LHM payload).        */
                    RTK_LOGE(TAG, "Line truncated, resetting\n");
                    line_pos = 0;
                }
            }
        }

        usb_cdc_line_pos = line_pos;
    }

    /* Unreachable in normal operation — cleanup stubs for completeness */
    usbd_cdc_acm_deinit();
exit_usbd:
    usbd_deinit();
exit_sema:
    rtos_sema_delete(usb_cdc_rx_sema);
exit_task:
    rtos_task_delete(NULL);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

void usb_cdc_receiver_start(void)
{
    int ret;
    rtos_task_t task;

    ret = rtos_task_create(&task,
                           "usb_cdc_rx",
                           usb_cdc_rx_task,
                           NULL,
                           USB_CDC_TASK_STACK_SIZE,
                           USB_CDC_TASK_PRIORITY);
    if (ret != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "Failed to create USB CDC receiver task\n");
    }
    else
    {
        RTK_LOGI(TAG, "USB CDC receiver task created\n");
    }
}

#endif /* CONFIG_USB_CDC_MODE */
