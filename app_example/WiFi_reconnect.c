#include "WiFi_reconnect.h"
#include "rtw_skbuff.h"
u8 retry_cnt = 0;

#define TAG         "WIFI_RECONNECT"
int user_WiFi_connect()
{
    /**
  * @brief    Describes connection settings for connecting to an AP
  * @note
  *        1. If used for Wi-Fi connect, setting `channel` to 0 means full channel scan;
  *           setting `channel` to a specific value means do active scan on that channel.
  *        2. Set `pscan_option` to @ref RTW_PSCAN_FAST_SURVEY for fast survey (active scan on
  *           specified channel, 25ms each, up to 7 attempts); 0 for normal scan.
  */
  // struct rtw_network_info {
  //     struct rtw_ssid                ssid;  /**< AP's SSID (max length: @ref RTW_ESSID_MAX_SIZE). */
  //     struct rtw_mac                bssid; /**< AP's MAC address. */
  //     u32                            security_type; /**< Necessarily set for WEP (@ref RTW_SECURITY_WEP_PSK, @ref RTW_SECURITY_WEP_SHARED). Auto-adjusted for others. */
  //     u8                           *password;       /**< AP's password. */
  //     s32                         password_len;  /**< Password length (max: @ref RTW_MAX_PSK_LEN). */
  //     s32                         key_id;           /**< WEP key ID (0-3). Only for WEP.*/
  //     u8                            channel;       /**< 0 for full scan, other values to scan specific channel. */
  //     u8                             pscan_option;    /**< @ref RTW_PSCAN_FAST_SURVEY for fast survey, 0 for normal scan. */
  //     u8                             is_wps_trigger;    /**< Indicates if connection is triggered by WPS. */
  //     struct rtw_wpa_supp_connect    wpa_supp;   /**< Used by Linux host for STA connect details (not used by RTOS). */
  //     struct rtw_mac                prev_bssid; /**< BSSID of the AP before roaming. */
  //     u8                            by_reconn; /**< Indicates if connection is triggered by auto-reconnect. */
  //     u8                            rom_rsvd[4];
  // };
    struct rtw_network_info connect_param = { 0 };
    /*Connect parameter set*/
    memcpy(connect_param.ssid.val, (char*)SSID, strlen(SSID));/**< SSID value, terminated with a null character.*/
    connect_param.ssid.len = strlen(SSID);
    connect_param.password = (unsigned char*)PASSWORD;//u8
    connect_param.password_len = strlen(PASSWORD);
    int ret = 0;

WIFI_CONNECT:
    /*Connect*/
    RTK_LOGI(TAG, "Wifi connect start, retry cnt = %d\r\n", retry_cnt);
    ret = wifi_connect(&connect_param, 1);// 1 /* step2: malloc and set synchronous connection related variables*/
    if (ret != RTK_SUCCESS)
    {
        RTK_LOGI(TAG, "Reconnect Fail:%d\r\n", ret);
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
    }

    /*DHCP*/
    if (ret == RTK_SUCCESS)
    {
        RTK_LOGI(TAG, "Wifi connect success, Start DHCP\n");
        ret = COMPAT_REQUEST_IP(NETIF_WLAN_STA_INDEX);
        gpio_toggle((u32)LED1_PIN, 0);//DHCP wait for 500ms
        if (ret == DHCP_ADDRESS_ASSIGNED)
        {
            RTK_LOGI(TAG, "DHCP Success\r\n");
            retry_cnt = 0;
            return RTK_SUCCESS;
        }
        else
        {
            RTK_LOGI(TAG, "DHCP Fail\r\n");
            wifi_disconnect();
        }
    }

    /*Reconnect when connect fail or DHCP fail*/
    retry_cnt++;
    if (retry_cnt >= RETRY_LIMIT)
    {
        RTK_LOGI(TAG, "Reconnect limit reach, Wifi connect fail\r\n");
        return RTK_FAIL;
    }
    else
    {
        //rtos_time_delay_ms(RETRY_INTERVAL);
        gpio_toggle((u32)LED2_PIN, RETRY_INTERVAL);
        goto WIFI_CONNECT;
    }
}
void gpio_led_init()
{
    GPIO_InitTypeDef led1_gpio; //Green light
    GPIO_InitTypeDef led2_gpio;    //Red light

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
        GPIO_WriteBit(LED2_PIN, 0);
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

void WiFi_connect_task()
{
    RTK_LOGI(TAG, "start\r\n");

    /* Wait wifi init finish
    Check if the specified wlan interface is running.
    1 running,0 is not, softap interface is 1,sta iface = 0*/
    while (!(wifi_is_running(STA_WLAN_INDEX)))
    {
        gpio_toggle((u32)LED2_PIN, 200);
    }
    GPIO_WriteBit(LED2_PIN, 1);
    /* Start connect */
    if (user_WiFi_connect() != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "user_WiFi_connect failed!\r\n");
        GPIO_WriteBit(LED2_PIN, 0);
    }
    else
    {
        GPIO_WriteBit(LED2_PIN, 0);
        GPIO_WriteBit(LED1_PIN, 1);
    }

    rtos_task_delete(NULL);
}

struct rtw_event_hdl_func_t event_external_hdl[1] =
{
    {RTW_EVENT_JOIN_STATUS,            WiFi_join_status_event_hdl},
};
u16 array_len_of_event_external_hdl = sizeof(event_external_hdl) / sizeof(struct rtw_event_hdl_func_t);

//WiFi connection status changes trigger this event handler; reconnect to AP is based on this function
void WiFi_join_status_event_hdl(u8* evt_info)
{
    struct rtw_event_join_status_info* join_status_info = (struct rtw_event_join_status_info*)evt_info;
    u8 join_status = join_status_info->status;
    struct rtw_event_disconnect* disconnect;

    /*Reconnect when disconnect after connected*/
    if (join_status == RTW_JOINSTATUS_DISCONNECT)
    {
        disconnect = &join_status_info->priv.disconnect;
        GPIO_WriteBit(LED1_PIN, 0);
        /*Disconnect by APP no need do reconnect*/
        /*RTK defined: Application layer call some API to cause wifi disconnect.*/
        if (disconnect->disconn_reason > RTW_DISCONN_RSN_APP_BASE && disconnect->disconn_reason < RTW_DISCONN_RSN_APP_BASE_END)
        {
            GPIO_WriteBit(LED2_PIN, 1);
            return;
        }

        /*Creat a task to do wifi reconnect because call WIFI API in WIFI event is not safe*/
        if (rtos_task_create(NULL, "WiFi_reconnect_task", (rtos_task_t)WiFi_reconnect_task, NULL, 2048, tskIDLE_PRIORITY + 1) != RTK_SUCCESS)
        {
            RTK_LOGI(TAG, "Create reconnect task failed\n");
        }
    }
}

void WiFi_reconnect_task()
{
    //do reconnect,call wifi connect func
    gpio_toggle((u32)LED2_PIN, RETRY_INTERVAL);
    if (user_WiFi_connect() != RTK_SUCCESS)
    {
        RTK_LOGE(TAG, "user_WiFi_connect failed!\r\n");
        GPIO_WriteBit(LED2_PIN, 0);
    }
    else
    {
        GPIO_WriteBit(LED2_PIN, 0);
        GPIO_WriteBit(LED1_PIN, 1);
    }

    rtos_task_delete(NULL);
}
