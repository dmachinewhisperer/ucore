# Set location of the ESP32 port directory.
if(NOT MICROPY_PORT_DIR)
    set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../lib/micropython/ports/esp32)
endif()

include(${MICROPY_PORT_DIR}/esp32_common.cmake)
