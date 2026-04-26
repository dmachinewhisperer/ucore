# ucore is the IDF component carrying both MicroPython's esp32 port
# and the ucore runtime. MicroPython's esp32_common.cmake calls
# idf_component_register itself (a built-in constraint of the MP port
# build), so we extend the resulting component target with ucore's own
# sources rather than registering a second component.

set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/micropython/ports/esp32)

# ucore's headers must be visible to MicroPython's QSTR preprocessor
# (a separate cmake custom command in mkrules.cmake that snapshots its
# include path before idf_component_register runs). MICROPY_CPP_INC_EXTRA
# is read in scope here, before the snapshot.
list(APPEND MICROPY_CPP_INC_EXTRA ${CMAKE_CURRENT_LIST_DIR}/include)

include(${MICROPY_PORT_DIR}/esp32_common.cmake)
# esp32_common.cmake just called idf_component_register; the ucore IDF
# component now owns all of MP's port sources, and MICROPY_TARGET ==
# ucore's COMPONENT_TARGET.

# ESP-IDF runs every component's CMakeLists in scripting mode first to
# discover REQUIRES. target_sources/target_compile_options aren't
# scriptable, so skip the rest of the wiring on that pass.
if(CMAKE_BUILD_EARLY_EXPANSION)
    return()
endif()

# Add ucore's own sources to the same target.
target_sources(${MICROPY_TARGET} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ucore.c
    ${CMAKE_CURRENT_LIST_DIR}/jmp_bin.c
    ${CMAKE_CURRENT_LIST_DIR}/mpy_bindings.c
    ${CMAKE_CURRENT_LIST_DIR}/transport_uart.c
    ${CMAKE_CURRENT_LIST_DIR}/utils.c
)

# PUBLIC so consumers (e.g., main) AND ucore's own sources resolve
# `ucore/foo.h`. Also satisfies MP's regular compile, which needs to
# resolve board headers' `#include "ucore/mpconfig_ucore.h"`.
target_include_directories(${MICROPY_TARGET} PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ucore provides its own app_main, mp_task, and esp_native_code_commit
# in firmware/main/main.c and mpy_bindings.c. MP's port main.c also
# defines them, so we drop it from the source list to avoid multiple-
# definition link errors. (MicroPython exposes no flag for this.)
get_property(_srcs TARGET ${MICROPY_TARGET} PROPERTY SOURCES)
list(FILTER _srcs EXCLUDE REGEX "/ports/esp32/main\\.c$")
set_property(TARGET ${MICROPY_TARGET} PROPERTY SOURCES ${_srcs})

# Force-include mpy_bindings.h so MicroPython sources see the ucore
# function decls (mpyruntime_log, readline_over_jmp) that the board
# header's macros expand to.
target_compile_options(${MICROPY_TARGET} PRIVATE
    "-include${CMAKE_CURRENT_LIST_DIR}/include/ucore/mpy_bindings.h"
)

# On targets with native USB (S2/S3/P4), MP routes tinyusb's tusb_config.h
# through MP's mpconfigport.h → board mpconfigboard.h → ucore/mpconfig_ucore.h.
# tinyusb is a separate managed component — it doesn't inherit ucore's
# PUBLIC includes (it's our dependency, not our dependent). Plumb the
# include path explicitly when the tinyusb target is in the build.
idf_component_get_property(_tusb_lib espressif__tinyusb COMPONENT_LIB)
if(_tusb_lib)
    target_include_directories(${_tusb_lib} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
    )
endif()
