FILE(GLOB_RECURSE nofrendo_sources ${CMAKE_CURRENT_LIST_DIR}/src/*.*)

idf_component_register(
    SRCS ${nofrendo_sources}
    INCLUDE_DIRS
        "include"
        "include/nofrendo"
    PRIV_INCLUDE_DIRS
        "src"
        "${CMAKE_CURRENT_LIST_DIR}/../../include"
        "${CMAKE_CURRENT_LIST_DIR}/../../include/driver"
        "${CMAKE_CURRENT_LIST_DIR}/../../include/apps/nes"
    REQUIRES
        esp_timer
        driver
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -Wall -Wno-error>
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wno-error>
)
