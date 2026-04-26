# Set location of the ESP32 port directory.
if(NOT MICROPY_PORT_DIR)
    set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/../ucore/micropython/ports/esp32)
endif()

# Make ucore's public headers visible to MicroPython's QSTR preprocessor
# (a separate cmake custom command in mkrules.cmake that snapshots its
# include path here, before idf_component_register runs).
list(APPEND MICROPY_CPP_INC_EXTRA ${CMAKE_CURRENT_LIST_DIR}/../ucore/include)

include(${MICROPY_PORT_DIR}/esp32_common.cmake)
