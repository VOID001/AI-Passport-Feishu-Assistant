set(passport_ble_bridge_source "passport_ble_bridge.c")
if(CONFIG_AI_PASSPORT_QEMU)
    set(passport_ble_bridge_source "passport_ble_bridge_qemu.c")
endif()

idf_component_register(
    SRCS "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "work_assistant.c"
         "work_assistant_flow.c"
         "work_assistant_model.c"
         "work_assistant_settings.c"
         "legacy_nvs_migration.c"
         "${passport_ble_bridge_source}"
         "passport_ble_protocol.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_ble.c"
         "demo_low_power.c"
         "../assets/fonts/lv_font_passport_zh_14.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_driver_uart esp_hw_support esp_timer json mbedtls nvs_flash
)
