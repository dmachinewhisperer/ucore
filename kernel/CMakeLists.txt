cmake_minimum_required(VERSION 3.12)

set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/lib/micropython/ports/esp32)

# Use the ucore custom board definition.
# The board config header (mpconfigboard.h) carries all MicroPython overrides
# that ucore needs (stdout redirect, readline redirect, disabled peripherals).
set(MICROPY_BOARD UCORE_ESP32)
set(MICROPY_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/boards/${MICROPY_BOARD})

if(NOT EXISTS ${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
    message(FATAL_ERROR "Invalid MICROPY_BOARD specified: ${MICROPY_BOARD}")
endif()

# Define the output sdkconfig so it goes in the build directory.
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)

# Save the manifest file set from the cmake command line.
set(MICROPY_USER_FROZEN_MANIFEST ${MICROPY_FROZEN_MANIFEST})

# Include board config; this sets SDKCONFIG_DEFAULTS and optionally IDF_TARGET.
include(${MICROPY_BOARD_DIR}/mpconfigboard.cmake)

# Set the frozen manifest file.
if (MICROPY_USER_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_USER_FROZEN_MANIFEST})
elseif (NOT MICROPY_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_PORT_DIR}/boards/manifest.py)
endif()

# Concatenate all sdkconfig files into a combined one for the IDF to use.
file(WRITE ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "")
foreach(SDKCONFIG_DEFAULT ${SDKCONFIG_DEFAULTS})
    file(READ ${MICROPY_PORT_DIR}/${SDKCONFIG_DEFAULT} CONTENTS)
    file(APPEND ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "${CONTENTS}")
endforeach()
configure_file(${CMAKE_BINARY_DIR}/sdkconfig.combined.in ${CMAKE_BINARY_DIR}/sdkconfig.combined COPYONLY)
set(SDKCONFIG_DEFAULTS ${CMAKE_BINARY_DIR}/sdkconfig.combined)

set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common"
)

# Exclude MicroPython's main.c — ucore provides its own app_main() and mp_task().
set(MICROPY_COMPONENT_BUILD ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ucore)

# mpy_bindings.h declares functions (readline_over_jmp, mpyruntime_log) that
# the board header's macros redirect to. These need to be visible in
# MicroPython source files that expand those macros.
idf_component_get_property(MICROPY_TARGET mpyruntime COMPONENT_LIB)
if(MICROPY_TARGET)
    target_compile_options(${MICROPY_TARGET} PRIVATE
        "-include${CMAKE_CURRENT_LIST_DIR}/components/ucore/include/ucore/mpy_bindings.h"
    )
else()
    message(FATAL_ERROR "MicroPython target not found")
endif()
