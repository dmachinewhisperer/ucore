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

# ucore provides its own app_main, mp_task, and esp_native_code_commit
# in firmware/main/main.c and components/ucore/mpy_bindings.c. MP's port
# main.c also defines them, so we drop it from mpyruntime's sources to
# avoid multiple-definition link errors. (MicroPython exposes no flag
# to suppress its main.c, so we filter at the cmake target level.)
idf_component_get_property(_mpy_lib mpyruntime COMPONENT_LIB)
get_property(_mpy_sources TARGET ${_mpy_lib} PROPERTY SOURCES)
list(FILTER _mpy_sources EXCLUDE REGEX "/ports/esp32/main\\.c$")
set_property(TARGET ${_mpy_lib} PROPERTY SOURCES ${_mpy_sources})

# mpy_bindings.h declares functions (readline_over_jmp, mpyruntime_log) that
# the board header's macros redirect to. These need to be visible in
# MicroPython source files that expand those macros.
idf_component_get_property(MICROPY_TARGET mpyruntime COMPONENT_LIB)
if(MICROPY_TARGET)
    # Force-include mpy_bindings.h so MicroPython sources see the ucore
    # function decls (mpyruntime_log, readline_over_jmp) that the board
    # header's macros expand to.
    target_compile_options(${MICROPY_TARGET} PRIVATE
        "-include${CMAKE_CURRENT_LIST_DIR}/components/ucore/include/ucore/mpy_bindings.h"
    )
    # Make ucore's public headers resolvable from MicroPython sources so
    # board headers can `#include "ucore/mpconfig_ucore.h"`. There's no
    # REQUIRES edge from mpyruntime → ucore (would be a cycle), so the
    # include path has to be plumbed manually.
    target_include_directories(${MICROPY_TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/components/ucore/include
    )
else()
    message(FATAL_ERROR "MicroPython target not found")
endif()
