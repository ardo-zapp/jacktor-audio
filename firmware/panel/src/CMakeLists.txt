FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "../include" "../include/driver" "../include/apps/nes" "../include/apps/msc" "../include/apps/file_manager" "../include/apps/dac" "../include/apps/player" "../include/views" "." "driver" "apps/nes" "apps/msc" "apps/file_manager" "apps/settings" "apps/dac" "apps/player" "views" "comms"
    REQUIRES nvs_flash driver esp_timer cjson esp_adc lvgl micro_flac micro_mp3 nofrendo esp_lcd esp_lcd_ili9341 esp_lcd_touch esp_lcd_touch_xpt2046
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -Wall -Werror>
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wno-old-style-declaration -Wno-error>
)
