#include <platform_autoconf.h>

#ifndef CONFIG_USB_CDC_MODE

#include "core/wifi_reconnect.h"

/* rtw_reconn / wifi_user_config — SDK auto reconnect globals.
 * wifi_auto_reconnect.h provides struct rtw_auto_reconn_t definition
 * and the extern declaration for rtw_reconn.                           */
#if CONFIG_AUTO_RECONNECT
#include <wifi_auto_reconnect.h>
#endif

/* WiFi status flag — shared with UI timer for waiting→monitor transition.
 * Declared extern here to avoid pulling in pc_dashboard.h (TAG conflict). */
extern volatile bool g_wifi_connected;

/* g_wifi_retry_exhausted is defined in pc_dashboard.c and declared extern via
 * wifi_reconnect.h — we reference it here but do not own the definition. */

/* Retry progress counters — updated by wifi_retry_periodic_check() */
#if CONFIG_AUTO_RECONNECT
volatile int g_wifi_retry_current = 0;
volatile int g_wifi_retry_max     = 0;
#endif

#define TAG "WIFI_RECONNECT"

int user_wifi_connect()
{
    struct rtw_network_info connect_param = { 0 };
    /*Connect parameter set*/
    memcpy(connect_param.ssid.val, (char*) SSID, strlen(SSID)); /**< SSID value, terminated with a null character.*/
    connect_param.ssid.len     = strlen(SSID);
    connect_param.password     = (unsigned char*) PASSWORD; // u8
    connect_param.password_len = strlen(PASSWORD);
    int ret;

    /* Single connect attempt.
     *
     * NOTE: no retry loop here.  If this attempt fails the caller
     * (wifi_connect_task) reports the result and exits.  Reconnection
     * after a later disconnection is handled by the SDK internal auto
     * reconnect (CONFIG_AUTO_RECONNECT).                                */
    RTK_LOGI(TAG, "Wifi connect start\n");
    ret = wifi_connect(&connect_param, 1); // 1 /* step2: malloc and set synchronous connection related variables*/
    if (ret != RTK_SUCCESS)
    {
        RTK_LOGI(TAG, "Connect Failed:%d\r\n", ret);
        if ((ret == -RTK_ERR_WIFI_CONN_INVALID_KEY))
        {
            RTK_LOGI(TAG, "(password format wrong)\r\n");
        }
        else if (ret == -RTK_ERR_WIFI_CONN_SCAN_FAIL)
        {
            RTK_LOGI(TAG, "(not found AP)\r\n");
        }
        else if (ret == -RTK_ERR_BUSY)
        {
            RTK_LOGI(TAG, "(busy)\r\n");
        }
        else
        {
            RTK_LOGI(TAG, "(other)\r\n");
        }
        return RTK_FAIL;
    }

    /*DHCP*/
    RTK_LOGI(TAG, "Wifi connect success, Start DHCP\n");
    ret = COMPAT_REQUEST_IP(NETIF_WLAN_STA_INDEX);
    gpio_toggle((u32) LED1_PIN, 0); // DHCP wait for 500ms
    if (ret == DHCP_ADDRESS_ASSIGNED)
    {
        RTK_LOGI(TAG, "DHCP Success\r\n");
        g_wifi_connected = true;
        return RTK_SUCCESS;
    }
    else
    {
        RTK_LOGI(TAG, "DHCP Fail\r\n");
        wifi_disconnect();
        return RTK_FAIL;
    }
}

void gpio_led_init()
{
    GPIO_InitTypeDef led1_gpio; // Green light
    GPIO_InitTypeDef led2_gpio; // Red light

    led1_gpio.GPIO_Pin = LED1_PIN;
    led2_gpio.GPIO_Pin = LED2_PIN;

    led1_gpio.GPIO_Mode = GPIO_Mode_OUT;
    led2_gpio.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_Init(&led1_gpio);
    GPIO_Init(&led2_gpio);

    GPIO_WriteBit(LED1_PIN, 0);
    GPIO_WriteBit(LED2_PIN, 0);
    return;
}

void gpio_toggle(u32 GPIO_Pin, int time_ms)
{
    if (time_ms == 0)
    {
        GPIO_WriteBit(GPIO_Pin, 0);
        for (int i = 0; i < 10; i++)
        {
            GPIO_WriteBit(GPIO_Pin, !GPIO_ReadDataBit(GPIO_Pin));
            rtos_time_delay_ms(50);
        }
        return;
    }

    while (time_ms > 0)
    {
        GPIO_WriteBit(GPIO_Pin, !GPIO_ReadDataBit(GPIO_Pin));
        rtos_time_delay_ms(200);
        time_ms = time_ms - 200;
    }
    GPIO_WriteBit(LED2_PIN, 1);
    return;
}

void wifi_connect_task()
{
    RTK_LOGI(TAG, "start\r\n");
#if CONFIG_AUTO_RECONNECT
    wifi_set_autoreconnect(1);
    RTK_LOGI(TAG, "SDK auto reconnect enabled\n");
#endif

    while (!(wifi_is_running(STA_WLAN_INDEX)))
    {
        gpio_toggle((u32) LED2_PIN, 200);
    }
    GPIO_WriteBit(LED2_PIN, 1);

    /* Retry initial connect — SDK auto reconnect only applies AFTER a
     * DISCONNECT event, so we must retry the initial connect ourselves.  */
#if CONFIG_AUTO_RECONNECT
    g_wifi_retry_max = wifi_user_config.auto_reconnect_count;
    for (int retry = 0; retry <= g_wifi_retry_max; retry++)
    {
        /* Update UI counter (rtw_reconn.cnt is 0 before first DISCONNECT) */
        g_wifi_retry_current = retry;
#else
    /* No SDK auto-reconnect — try once only */
    {
#endif
        if (user_wifi_connect() == RTK_SUCCESS)
        {
            g_wifi_connected = true;
        #if CONFIG_AUTO_RECONNECT
            g_wifi_retry_current = 0;
        #endif
            GPIO_WriteBit(LED2_PIN, 0);
            GPIO_WriteBit(LED1_PIN, 1);
            goto connect_done;
        }

#if CONFIG_AUTO_RECONNECT
        gpio_toggle((u32) LED2_PIN, 0); /* flash red LED while retrying */
        rtos_time_delay_ms(5000);
    }

    /* All retries exhausted */
    g_wifi_retry_exhausted = true;
    g_wifi_retry_current   = g_wifi_retry_max;
    RTK_LOGE(TAG, "All %d connect attempts failed!\r\n", g_wifi_retry_max + 1);
#else
    RTK_LOGE(TAG, "user_wifi_connect failed!\r\n");
#endif /* CONFIG_AUTO_RECONNECT */

connect_done:
    rtos_task_delete(NULL);
}

struct rtw_event_hdl_func_t event_external_hdl[1] = {
    { RTW_EVENT_JOIN_STATUS, wifi_join_status_event_hdl },
};
u16 array_len_of_event_external_hdl = sizeof(event_external_hdl) / sizeof(struct rtw_event_hdl_func_t);


void wifi_join_status_event_hdl(u8* evt_info)
{
    struct rtw_event_join_status_info* join_status_info = (struct rtw_event_join_status_info*) evt_info;
    u8                                 join_status      = join_status_info->status;

    if (join_status == RTW_JOINSTATUS_SUCCESS)
    {
        g_wifi_connected       = true;
        g_wifi_retry_exhausted = false;
    #if CONFIG_AUTO_RECONNECT
        g_wifi_retry_current   = 0;
    #endif
        GPIO_WriteBit(LED2_PIN, 0);
        GPIO_WriteBit(LED1_PIN, 1);
        RTK_LOGI(TAG, "WiFi connected (via event)\n");
    }
    else if (join_status == RTW_JOINSTATUS_DISCONNECT)
    {
        struct rtw_event_disconnect* disconnect = &join_status_info->priv.disconnect;
        GPIO_WriteBit(LED1_PIN, 0);
        g_wifi_connected = false;

        if (disconnect->disconn_reason > RTW_DISCONN_RSN_APP_BASE &&
            disconnect->disconn_reason < RTW_DISCONN_RSN_APP_BASE_END)
        {
            GPIO_WriteBit(LED2_PIN, 1);
            return;
        }

        RTK_LOGI(TAG, "WiFi disconnected — SDK auto reconnect ongoing\n");
    }
}

/* ========================================================================
 * Periodic check — call from MQTT park loop or LVGL timer (~1s interval)
 *
 * 1. Update g_wifi_retry_current / g_wifi_retry_max for UI progress
 * 2. Poll wifi_get_join_status() to detect SDK reconnection
 * 3. Already flagged exhausted → try user_wifi_connect() recovery
 * 4. NOT exhausted → check rtw_reconn.cnt to detect exhaustion
 * ======================================================================== */
void wifi_retry_periodic_check(void)
{
    if (g_wifi_connected)
        return;

    /* Update retry counters for UI — mirror SDK auto-reconnect progress.
     * Guard: only overwrite current when rtw_reconn.cnt > 0.  During
     * initial connect (before any DISCONNECT event) rtw_reconn.cnt is 0,
     * and wifi_connect_task drives the count via its own retry loop.     */
#if CONFIG_AUTO_RECONNECT
    if (rtw_reconn.cnt > 0)
        g_wifi_retry_current = rtw_reconn.cnt;
    g_wifi_retry_max = wifi_user_config.auto_reconnect_count;
#endif

    /* Poll: if reconnected already, mark connected and done */
    {
        u8 join_status;
        if (wifi_get_join_status(&join_status) == RTK_SUCCESS &&
            join_status == RTW_JOINSTATUS_SUCCESS)
        {
            g_wifi_connected = true;
            g_wifi_retry_exhausted = false;
        #if CONFIG_AUTO_RECONNECT
            g_wifi_retry_current = 0;
        #endif
            GPIO_WriteBit(LED2_PIN, 0);
            GPIO_WriteBit(LED1_PIN, 1);
            return;
        }
    }

    if (g_wifi_retry_exhausted)
        return; /* show UI popup, no more retries */

    /* Detect SDK exhaustion: cnt > max and not mid-retry */
#if CONFIG_AUTO_RECONNECT
    if (rtw_reconn.cnt > wifi_user_config.auto_reconnect_count &&
        rtw_reconn.b_waiting == 0 && rtw_reconn.b_ongoing == 0)
    {
        g_wifi_retry_exhausted = true;
        g_wifi_retry_current   = wifi_user_config.auto_reconnect_count;
    }
#endif
}

#endif /* !CONFIG_USB_CDC_MODE */
