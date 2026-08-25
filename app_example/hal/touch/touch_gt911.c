/*
 * touch_gt911.c — Goodix GT911 capacitive touch controller
 *
 * Self-contained driver for the T1720A screen port.
 * Integrates directly with LVGL via lv_indev_t (no SDK input manager dependency).
 *
 * Pin assignment (from SDK input_touch_gt911.c):
 *   RST  = _PB_0
 *   INT  = _PA_31
 *   SDA  = _PA_29  (I2C0)
 *   SCL  = _PA_30  (I2C0)
 */

#include "touch_gt911.h"
#include "lvgl.h"
#include "ameba_soc.h"
#include "os_wrapper.h"
#include "gpio_irq_api.h"
#include "i2c_api.h"
#include "i2c_ex_api.h"

#define LOG_TAG "GT911"

/* ---- Pin assignment ---- */
#define RST_PIN                 _PB_0
#define INT_PIN                 _PA_31
#define SDA_PIN                 _PA_29
#define SCL_PIN                 _PA_30

/* ---- Touch resolution (T1720A panel is 800x480 landscape) ---- */
#define TOUCH_XSIZE             800
#define TOUCH_YSIZE             480

/* ---- Coordinate transform flags ---- */
#define TRANSFORM_EXCHANGE_X_Y  0
#define TRANSFORM_INVERSE_X     0
#define TRANSFORM_INVERSE_Y     0

/* ---- GT911 register map ---- */
#define GT_CTRL_REG             0X8040
#define GT_CFGS_REG             0X8047
#define GT_CHECK_REG            0X80FF
#define GT_PID_REG              0X8140
#define GT_GSTID_REG            0X814E
#define GT_TP1_REG              0X8150
#define GT_POINT_REG            0x814F

#define TPD_MAX_FINGERS         5
#define I2C_ADDR                0x14
#define I2C_BUS_CLK             400000

#define MSG_Q_SIZE              20

/* ---- Local state ---- */
typedef struct
{
    uint16_t    x;
    uint16_t    y;
    bool        initialized;
    bool        enabled;
    gpio_irq_t  gpio_irq;
    i2c_t       client;
    rtos_queue_t work_queue;
    rtos_mutex_t lock;
} gt911_data_t;

static gt911_data_t      s_gt911;
static lv_indev_data_t   s_lvgl_touch_data = {
    .state = LV_INDEV_STATE_RELEASED,
    .point = {0, 0}
};

/* ---- I2C read/write helpers (identical to SDK) ---- */

static int gt911_i2c_read(i2c_t *client, uint16_t reg, uint8_t *buf, int len)
{
    uint16_t r;
    int ret = 0;

    r = reg & 0xff;
    reg = (reg >> 8) | (r << 8);
    ret = i2c_write(client, I2C_ADDR, (char *)&reg, 2, 1);

    if (ret != 2)
    {
        RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "%s: Slave no ACK before read. \r\n", __func__);
        return -1;
    }

    return i2c_read(client, I2C_ADDR, (char *)buf, len, 1);
}

static int gt911_i2c_write(i2c_t *client, uint16_t reg, uint8_t *buf, int len)
{
    uint8_t *temp;
    int ret = 0;
    temp = rtos_mem_zmalloc(len + 2);
    temp[1] = reg & 0xff;
    temp[0] = (reg >> 8);
    memcpy(temp + 2, buf, len);
    ret = i2c_write(client, I2C_ADDR, (char *)temp, len + 2, 2);
    rtos_mem_free(temp);

    if (ret != (len + 2))
    {
        RTK_LOGS(NOTAG, RTK_LOG_ALWAYS, "%s: Write to slave error. \r\n", __func__);
        return -1;
    }

    return 1;
}

static int gt911_read_reg(i2c_t *client, uint16_t reg, uint8_t *value)
{
    return gt911_i2c_read(client, reg, value, 1);
}

static int gt911_write_reg(i2c_t *client, uint16_t reg, uint8_t value)
{
    return gt911_i2c_write(client, reg, &value, 1);
}

#if 0
/* ---- GT911 configuration table (static, avoids conflict with SDK's non-static copy) ---- */
static const uint8_t GT911_CFG_TBL[] =
{
    0X60, 0XE0, 0X01, 0XE0, 0X01, 0X05, 0X35, 0X00, 0X02, 0X08,
    0X1E, 0X08, 0X05, 0X3C, 0X0F, 0X05, 0X00, 0X00, 0XFF, 0X67,
    0X50, 0X00, 0X00, 0X18, 0X1A, 0X1E, 0X14, 0X89, 0X28, 0X0A,
    0X30, 0X2E, 0XBB, 0X0A, 0X03, 0X00, 0X00, 0X02, 0X33, 0X1D,
    0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X32, 0X00, 0X00,
    0X2A, 0X1C, 0X5A, 0X94, 0XC5, 0X02, 0X07, 0X00, 0X00, 0X00,
    0XB5, 0X1F, 0X00, 0X90, 0X28, 0X00, 0X77, 0X32, 0X00, 0X62,
    0X3F, 0X00, 0X52, 0X50, 0X00, 0X52, 0X00, 0X00, 0X00, 0X00,
    0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00,
    0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X0F,
    0X0F, 0X03, 0X06, 0X10, 0X42, 0XF8, 0X0F, 0X14, 0X00, 0X00,
    0X00, 0X00, 0X1A, 0X18, 0X16, 0X14, 0X12, 0X10, 0X0E, 0X0C,
    0X0A, 0X08, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00,
    0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00,
    0X00, 0X00, 0X29, 0X28, 0X24, 0X22, 0X20, 0X1F, 0X1E, 0X1D,
    0X0E, 0X0C, 0X0A, 0X08, 0X06, 0X05, 0X04, 0X02, 0X00, 0XFF,
    0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00,
    0X00, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF,
    0XFF, 0XFF, 0XFF, 0XFF,
};

/* ---- Send configuration to GT911 ---- */
static int gt911_set_config(i2c_t *client, uint8_t mode)
{
    uint8_t buf[2];
    uint8_t i = 0;

    buf[0] = 0;
    buf[1] = mode;

    for (i = 0; i < sizeof(GT911_CFG_TBL); i++)
    {
        buf[0] += GT911_CFG_TBL[i];
    }

    buf[0] = (~buf[0]) + 1;
    gt911_i2c_write(client, GT_CFGS_REG, (uint8_t *)GT911_CFG_TBL, sizeof(GT911_CFG_TBL));
    gt911_i2c_write(client, GT_CHECK_REG, buf, 2);

    RTK_LOGI(LOG_TAG, "Config sent (checksum: 0x%02x, mode: 0x%02x)\n", buf[0], buf[1]);
    return 0;
}
#endif

/* ---- Hardware reset sequence ---- */

static void board_i2c_init(void)
{
    GPIO_WriteBit(INT_PIN, 0);
    GPIO_WriteBit(RST_PIN, 0);
    DelayMs(10);
    GPIO_WriteBit(INT_PIN, 1);
    DelayUs(100);
    GPIO_WriteBit(RST_PIN, 1);
    DelayMs(5);
    GPIO_WriteBit(INT_PIN, 0);
    DelayMs(50);
}

static void gt911_reset(gt911_data_t *ts)
{
    (void) ts;

    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Pin  = RST_PIN;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&gpio_init);

    gpio_init.GPIO_Pin  = INT_PIN;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&gpio_init);

    board_i2c_init();

    gpio_init.GPIO_Pin  = INT_PIN;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio_init.GPIO_Mode = GPIO_Mode_IN;
    GPIO_Init(&gpio_init);

    /* Init I2C */
    ts->client.i2c_idx = 0;
    i2c_init(&ts->client, SDA_PIN, SCL_PIN);
    i2c_frequency(&ts->client, I2C_BUS_CLK);
    i2c_restart_disable(&ts->client);

#if 1 /* PATCH_FOR_LCD_NOISE */
    /* Set IC_FILTER manually to ignore LCD noise */
    extern I2C_InitTypeDef I2CInitDat[2];
    uint32_t val;

    I2C_Cmd(ts->client.I2Cx, DISABLE);
    I2CInitDat[ts->client.i2c_idx].I2CFilter = 0x108;
    val = HAL_READ32(0x41008000, 0xEC);
    val &= (~0x1FF);
    val |= 0x108;
    HAL_WRITE32(0x41008000, 0xEC, val);
    I2C_Cmd(ts->client.I2Cx, ENABLE);
#endif
}

/* ---- Read product info ---- */

static int gt911_read_product_info(gt911_data_t *ts)
{
    const char iic_write_buf[2] = {0x81, 0x40};
    char iic_read_buf[5] = {0};

    i2c_write(&ts->client, I2C_ADDR, iic_write_buf, 2, 1);
    i2c_read(&ts->client, I2C_ADDR, iic_read_buf, 4, 1);
    RTK_LOGI(LOG_TAG, "%s CTP ID:%x %x %x %x \r\n", __func__,
             iic_read_buf[0], iic_read_buf[1], iic_read_buf[2], iic_read_buf[3]);

    return 0;
}

/* ---- Process touch data (called from work queue) ---- */

static void gt911_process_touch_data(gt911_data_t *ts)
{
    if (!ts)
        return;

    uint8_t read[10] = {0};
    uint8_t mode = 0;
    uint8_t reset = 0;
    int ret = 0;
    int len = 0;

    rtos_mutex_take(ts->lock, MUTEX_WAIT_TIMEOUT);
    ret = gt911_read_reg(&ts->client, GT_GSTID_REG, &mode);

    if (ret < 0)
    {
        RTK_LOGS(LOG_TAG, RTK_LOG_ALWAYS, "process: GSTID read FAILED (ret=%d)\n", ret);
        goto err_finish;
    }

    read[0] = mode;

    if (mode & 0x80)
    {
        ret = gt911_write_reg(&ts->client, GT_GSTID_REG, reset);
        if (ret < 0)
        {
            RTK_LOGS(LOG_TAG, RTK_LOG_ALWAYS, "process: write clear GSTID FAILED\n");
            goto err_finish;
        }
    }
    else
    {
        goto err_finish;
    }

    if ((mode & 0x0F) == 0)
    {
        goto err_finish;
    }

    len = gt911_i2c_read(&ts->client, GT_POINT_REG, read + 1, 7);

    if (len < 0)
    {
        RTK_LOGS(LOG_TAG, RTK_LOG_ALWAYS, "process: read POINT_REG FAILED\n");
        goto err_finish;
    }

    ret = gt911_write_reg(&ts->client, GT_GSTID_REG, reset);
    if (ret < 0)
    {
        RTK_LOGS(LOG_TAG, RTK_LOG_ALWAYS, "process: final write GSTID FAILED\n");
        goto err_finish;
    }

err_finish:
    if (read[0] > 0)
    {
        uint8_t state = len > 0 ? 1 : 0;
        uint16_t x = (read[2] | (read[3] << 8));
        uint16_t y = (read[4] | (read[5] << 8));

        if (state)
        {
#if TRANSFORM_INVERSE_X
            x = TOUCH_XSIZE - x;
#endif
#if TRANSFORM_INVERSE_Y
            y = TOUCH_YSIZE - y;
#endif
#if TRANSFORM_EXCHANGE_X_Y
            {
                uint16_t tmp = x;
                x = y;
                y = tmp;
            }
#endif
            ts->x = x;
            ts->y = y;
        }
        else
        {
            x = ts->x;
            y = ts->y;
        }

        /* Feed into LVGL touch data */
        s_lvgl_touch_data.point.x = x;
        s_lvgl_touch_data.point.y = y;
        s_lvgl_touch_data.state   = state ? LV_INDEV_STATE_PRESSED
                                          : LV_INDEV_STATE_RELEASED;

        RTK_LOGD(LOG_TAG, "x:%d y:%d state:%d\n", x, y, state);
    }

    rtos_mutex_give(ts->lock);
}

/* ---- Work queue handler ---- */

static int gt911_work_handler(gt911_data_t *ts)
{
    void *p_msg = NULL;

    if (ts->enabled)
    {
        if (RTK_SUCCESS == rtos_queue_receive(ts->work_queue, &p_msg, 20))
        {
            gt911_process_touch_data(ts);
            return 0;
        }

        /* Poll INT pin to detect finger release. GT911 INT is active LOW
         * (open-drain): LOW = finger down, HIGH = finger up.  We only act when
         * LVGL still thinks the finger is PRESSED AND INT is HIGH (released).
         * This avoids the false RELEASED during swipe gaps where INT briefly
         * goes HIGH between report cycles — we just skip those gaps. */
        if (s_lvgl_touch_data.state == LV_INDEV_STATE_PRESSED)
        {
            uint32_t level = GPIO_ReadDataBit(INT_PIN);
            if (level != 0)
            {
                /* INT HIGH — finger genuinely released */
                s_lvgl_touch_data.state = LV_INDEV_STATE_RELEASED;
            }
        }
    }

    return -1;
}

/* ---- GPIO IRQ handler ---- */

static void gt911_irq_handler(uint32_t dev_id, uint32_t event)
{
    (void) event;
    gt911_data_t *ts = (gt911_data_t *) dev_id;
    if (!ts)
        return;

    rtos_queue_send(ts->work_queue, ts, 0);
}

/* ---- Work task ---- */

static void gt911_work_task(void *param)
{
    gt911_data_t *ts = (gt911_data_t *) param;
    if (!ts)
    {
        rtos_task_delete(NULL);
        return;
    }

    while (ts->initialized)
    {
        if (ts->enabled)
        {
            if (gt911_work_handler(ts))
            {
                rtos_time_delay_ms(20);
            }
        }
        else
        {
            rtos_time_delay_ms(20);
        }
    }

    rtos_task_delete(NULL);
}

/* ---- LVGL read callback ---- */

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void) indev;

    data->point.x = s_lvgl_touch_data.point.x;
    data->point.y = s_lvgl_touch_data.point.y;
    data->state   = s_lvgl_touch_data.state;

    if (data->state == LV_INDEV_STATE_PRESSED)
    {
        RTK_LOGS(LOG_TAG, RTK_LOG_DEBUG, "LVGL read: (%d,%d) PRESSED\n",
                 data->point.x, data->point.y);
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void touch_gt911_init(void)
{
    if (s_gt911.initialized)
        return;

    RTK_LOGI(LOG_TAG, "===== GT911 Touch Initialization =====\n");

    rtos_mutex_create_static(&s_gt911.lock);
    rtos_mutex_give(s_gt911.lock);
    rtos_queue_create(&s_gt911.work_queue, MSG_Q_SIZE, sizeof(uint32_t));

    /* Hardware init */
    gt911_reset(&s_gt911);
    rtos_time_delay_ms(100);
    gt911_read_product_info(&s_gt911);
    rtos_time_delay_ms(10);

    /* Ensure GT911 in normal mode (0 = normal, not config/sleep) */
    gt911_write_reg(&s_gt911.client, GT_CTRL_REG, 0);
    rtos_time_delay_ms(10);

    RTK_LOGI(LOG_TAG, "Registering IRQ on INT_PIN(_PA_31)...\n");
    gpio_irq_init(&s_gt911.gpio_irq, INT_PIN, gt911_irq_handler, (uint32_t)&s_gt911);
    gpio_irq_set(&s_gt911.gpio_irq, IRQ_FALL, 1);

    s_gt911.initialized = true;
    s_gt911.enabled     = true;

    /* Enable IRQ after init completes */
    gpio_irq_enable(&s_gt911.gpio_irq);

    /* Create work task */
    if (rtos_task_create(NULL, ((const char *)"gt911_work"),
                         gt911_work_task, &s_gt911,
                         1024 * 4, 3) != RTK_SUCCESS)
    {
        RTK_LOGE(LOG_TAG, "Failed to create gt911_work task\n\r");
    }

    /* Register with LVGL */
    lv_indev_t *indev_touch = lv_indev_create();
    if (indev_touch)
    {
        lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_touch, touch_read_cb);
        RTK_LOGI(LOG_TAG, "GT911 registered to LVGL\n");
    }
    else
    {
        RTK_LOGE(LOG_TAG, "Failed to create LVGL indev\n");
    }

    RTK_LOGI(LOG_TAG, "===== GT911 Touch Ready =====\n");
}
