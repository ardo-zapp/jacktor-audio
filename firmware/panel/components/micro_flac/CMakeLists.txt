idf_component_register(
    SRCS
        "src/crc.cpp"
        "src/decorrelation.cpp"
        "src/flac_decoder.cpp"
        "src/frame_header.cpp"
        "src/lpc.cpp"
        "src/pcm_packing.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17 -Wall -Werror -DMICRO_FLAC_DISABLE_OGG>
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wno-error -DMICRO_FLAC_DISABLE_OGG>
)
