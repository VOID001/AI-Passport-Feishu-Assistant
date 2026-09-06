set(bsp_audio_source "src/bsp_audio.c")
if(CONFIG_AI_PASSPORT_QEMU)
    set(bsp_audio_source "src/bsp_audio_qemu.c")
endif()

idf_component_register(
    SRCS "src/bsp_i2c.c"
         "src/bsp_display.c"
         "src/bsp_display_lvgl.c"
         "src/bsp_button.c"
         "${bsp_audio_source}"
         "src/bsp_battery.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_i2s esp_lcd esp_lvgl_port button esp_adc esp_codec_dev
)

# The simulation component only exists when tools/passport_emulator injects it.
idf_component_optional_requires(PRIVATE esp_lcd_qemu_rgb)
