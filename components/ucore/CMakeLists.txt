list(APPEND UCORE_SRCS
    "mpy_bindings.c"
    "ucore.c"
    "jmp_bin.c"
    "utils.c"
)


idf_component_register(
    SRCS
        ${UCORE_SRCS}
    INCLUDE_DIRS
        "include"
    REQUIRES  
        json
        # esp_websocket_client
        heap
        esp_event
        protocol_examples_common
        esp_partition
        nvs_flash
        esp_hw_support
        esp_psram
        lwip
        esp_timer
        mbedtls
        driver
        mpyruntime

        # it appears later version of micropython removed supported for adding micropython as a main_* component. 
        # main_${IDF_TARGET}
)
