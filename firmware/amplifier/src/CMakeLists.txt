FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "../include" "../include/driver" "../include/driver/kissfft" "../include/power" "../include/state" "../include/ota" "../include/comms" "." "driver" "driver/kissfft" "power" "state" "ota" "comms"
    REQUIRES nvs_flash driver esp_timer mbedtls bt cjson esp_adc esp_driver_i2s esp-idf-lib__ads111x esp-idf-lib__ds3231 espressif__ds18b20
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wall -Werror)
