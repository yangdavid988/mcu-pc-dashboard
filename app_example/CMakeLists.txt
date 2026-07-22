# Please refer to path/to/your/ameba_sdk/cmake/CMakeLists-template.cmake

# ===== Public: applies to ALL targets (including LVGL) =====
# Add project directory to global include path so lv_conf_project.h is findable
ameba_global_include(${CMAKE_CURRENT_SOURCE_DIR})
# Use project-specific lv_conf instead of SDK default
ameba_global_define(LV_CONF_PATH="lv_conf_project.h")

# ===== Screen selection (uncomment the next line to switch to DBL070 instead) =====
add_definitions(-DUSE_DBL070)

# ===== Private: builds our target =====
set(private_sources)
ameba_list_append(private_sources app_main.c pc_dashboard.c pc_dashboard_ui.c pc_dashboard_lock_screen.c WiFi_reconnect.c pc_dashboard_theme.c pc_dashboard_layout.c gpio_control.c
    img_icons/icon_arrow_down.c img_icons/icon_arrow_up.c img_icons/icon_battery.c img_icons/icon_bolt.c img_icons/icon_cpu.c
    img_icons/icon_disk.c img_icons/icon_fan.c img_icons/icon_gear.c img_icons/icon_globe.c img_icons/icon_gpu.c
    img_icons/icon_laptop.c img_icons/icon_list.c img_icons/icon_power_off.c img_icons/icon_ram.c img_icons/icon_temp.c
    img_icons/icon_user.c img_icons/icon_warning.c img_icons/icon_wifi.c
    img_bg/bg_cobalt.c img_bg/bg_inferno.c img_bg/bg_silicon.c img_bg/clock432.c
	    drv/lcd/lcd_drv.c
	    drv/lcd/lcdc_core.c
	    drv/lcd/st7262_cfg.c
	    drv/lcd/dbl070_cfg.c)
ameba_add_internal_library(app_example
    p_SOURCES
        ${private_sources}
)
