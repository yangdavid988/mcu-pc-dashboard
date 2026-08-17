# Please refer to path/to/your/ameba_sdk/cmake/CMakeLists-template.cmake

# ===== Public: applies to ALL targets (including LVGL) =====
# Add project directory to global include path so config/*.h is findable
ameba_global_include(${CMAKE_CURRENT_SOURCE_DIR})
# Use project-specific lv_conf instead of SDK default
ameba_global_define(LV_CONF_PATH="config/lv_conf_project.h")

# ===== Screen + Mode selection =====
# These are configured via Kconfig:  pc_dashboard_demo/Kconfig
#   - Screen:       CONFIG_SCREEN_ST7262 (default) / CONFIG_SCREEN_DBL070
#   - USB CDC mode: CONFIG_USB_CDC_MODE (depends on SCREEN_ST7262)
#
# NOTE: Kconfiglib uses OR-semantics for second-definition 'depends on'
# on existing Symbols, so secondary config blocks cannot constrain SDK
# symbols — they can only expand visibility.  Use bms menuconfig to
# manually set USB/WiFi options when switching between ST7262 (USB CDC)
# and DBL070 (WiFi) modes.
#
# The Kconfig pipeline generates platform_autoconf.h per core (KM4NS/KM4TZ)
# which is included globally via the SDK include path.  No add_definitions()
# is needed — platform_autoconf.h provides the CONFIG_* defines to all
# compilation units.

# ===== Private: builds our target =====
set(private_sources)
ameba_list_append(private_sources
    # main/
    main/app_main.c
    # core/
    core/pc_dashboard.c core/standby_manager.c core/weather.c core/wifi_reconnect.c core/usb_cdc_receiver.c
    # hal/usb/
    hal/usb/usbd_cdc_acm_custom.c
    # ui/
    ui/pc_dashboard_ui.c ui/pc_dashboard_lock_screen.c ui/pc_dashboard_theme.c ui/pc_dashboard_layout.c
    # hal/
    hal/backlight_ctrl.c hal/gpio_control.c
    hal/lcd/lcd_drv.c hal/lcd/lcdc_core.c hal/lcd/st7262_cfg.c hal/lcd/dbl070_cfg.c
    # assets/icons/
    assets/icons/icon_arrow_down.c assets/icons/icon_arrow_up.c assets/icons/icon_battery.c
    assets/icons/icon_bolt.c assets/icons/icon_cpu.c assets/icons/icon_disk.c
    assets/icons/icon_fan.c assets/icons/icon_gear.c assets/icons/icon_globe.c
    assets/icons/icon_gpu.c assets/icons/icon_laptop.c assets/icons/icon_list.c
    assets/icons/icon_power_off.c assets/icons/icon_ram.c assets/icons/icon_temp.c
    assets/icons/icon_user.c assets/icons/icon_warning.c assets/icons/icon_wifi.c
    assets/icons/icon_sun.c assets/icons/icon_cloud.c assets/icons/icon_rain.c
    assets/icons/icon_drizzle.c assets/icons/icon_thunderstorm.c assets/icons/icon_snow.c
    assets/icons/icon_fog.c
    # assets/backgrounds/
    assets/backgrounds/bg_cobalt.c assets/backgrounds/bg_inferno.c assets/backgrounds/bg_silicon.c
    assets/backgrounds/clock432.c
)
ameba_add_internal_library(app_example
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${BASEDIR}/component/usb/common
        ${BASEDIR}/component/usb/device/core
        ${BASEDIR}/component/usb/device/cdc_acm
)
