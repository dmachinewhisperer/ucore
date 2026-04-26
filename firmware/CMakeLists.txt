cmake_minimum_required(VERSION 3.12)

set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/components/ucore/micropython/ports/esp32)

# Pick a ucore board. Defaults to ESP32_GENERIC; override at configure
# time with -DMICROPY_BOARD=ESP32_GENERIC_S3 (etc.). Names follow
# MicroPython port board conventions.
if(NOT DEFINED MICROPY_BOARD)
    set(MICROPY_BOARD ESP32_GENERIC)
endif()
set(MICROPY_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/components/ucore/boards/${MICROPY_BOARD})

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
# CONFIG_PARTITION_TABLE_CUSTOM_FILENAME values from MicroPython's fragments
# are relative to project root, but the CSVs live under MICROPY_PORT_DIR.
# Rewrite any relative path to an absolute one under MICROPY_PORT_DIR so IDF
# resolves them in place — no need to copy CSVs into the project root.
file(WRITE ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "")
foreach(SDKCONFIG_DEFAULT ${SDKCONFIG_DEFAULTS})
    file(READ ${MICROPY_PORT_DIR}/${SDKCONFIG_DEFAULT} CONTENTS)
    string(REGEX REPLACE
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"([^/\"][^\"]*)\""
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${MICROPY_PORT_DIR}/\\1\""
        CONTENTS "${CONTENTS}")
    file(APPEND ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "${CONTENTS}")
endforeach()
configure_file(${CMAKE_BINARY_DIR}/sdkconfig.combined.in ${CMAKE_BINARY_DIR}/sdkconfig.combined COPYONLY)
set(SDKCONFIG_DEFAULTS ${CMAKE_BINARY_DIR}/sdkconfig.combined)

set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ucore)

# All MP↔ucore wiring lives inside components/ucore/CMakeLists.txt now.
# (Filter MP's main.c, force-include mpy_bindings.h, expose ucore/include.)
