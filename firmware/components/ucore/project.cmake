# Project-level setup for apps that consume the ucore component.
# Include this from your top-level CMakeLists.txt and call ucore_setup()
# before include($ENV{IDF_PATH}/tools/cmake/project.cmake).
#
# What it does, in order:
#   1. Picks a board (MICROPY_BOARD; default ESP32_GENERIC, override with -D).
#   2. Includes the board's mpconfigboard.cmake (sets IDF_TARGET +
#      SDKCONFIG_DEFAULTS).
#   3. Resolves the frozen manifest (defaults to MP's boards/manifest.py).
#   4. Concatenates MP's sdkconfig fragments into a single combined
#      defaults file, rewriting relative CONFIG_PARTITION_TABLE_CUSTOM_FILENAME
#      paths to absolute ones under MICROPY_PORT_DIR — so IDF resolves
#      partition CSVs in place without copying them to the project root.

# Resolve paths relative to this script (i.e. inside the ucore component).
set(_UCORE_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

macro(ucore_setup)
    set(MICROPY_PORT_DIR ${_UCORE_COMPONENT_DIR}/micropython/ports/esp32)

    if(NOT DEFINED MICROPY_BOARD)
        set(MICROPY_BOARD ESP32_GENERIC)
    endif()
    set(MICROPY_BOARD_DIR ${_UCORE_COMPONENT_DIR}/boards/${MICROPY_BOARD})

    if(NOT EXISTS ${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
        message(FATAL_ERROR "Invalid MICROPY_BOARD specified: ${MICROPY_BOARD}")
    endif()

    set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)

    # If the caller passed a frozen manifest on the cmake command line,
    # respect it; otherwise default to MP's per-port manifest.
    set(MICROPY_USER_FROZEN_MANIFEST ${MICROPY_FROZEN_MANIFEST})
    include(${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
    if(MICROPY_USER_FROZEN_MANIFEST)
        set(MICROPY_FROZEN_MANIFEST ${MICROPY_USER_FROZEN_MANIFEST})
    elseif(NOT MICROPY_FROZEN_MANIFEST)
        set(MICROPY_FROZEN_MANIFEST ${MICROPY_PORT_DIR}/boards/manifest.py)
    endif()

    file(WRITE ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "")
    foreach(SDKCONFIG_DEFAULT ${SDKCONFIG_DEFAULTS})
        file(READ ${MICROPY_PORT_DIR}/${SDKCONFIG_DEFAULT} CONTENTS)
        string(REGEX REPLACE
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"([^/\"][^\"]*)\""
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${MICROPY_PORT_DIR}/\\1\""
            CONTENTS "${CONTENTS}")
        file(APPEND ${CMAKE_BINARY_DIR}/sdkconfig.combined.in "${CONTENTS}")
    endforeach()
    configure_file(
        ${CMAKE_BINARY_DIR}/sdkconfig.combined.in
        ${CMAKE_BINARY_DIR}/sdkconfig.combined
        COPYONLY)
    set(SDKCONFIG_DEFAULTS ${CMAKE_BINARY_DIR}/sdkconfig.combined)

    # Bake board.md into the firmware as a frozen `ucore` Python module so
    # an agent can do `import ucore; print(ucore.INFO)` to learn the
    # platform without guessing pin numbers from the chip name.
    if(EXISTS ${MICROPY_BOARD_DIR}/board.md)
        file(READ ${MICROPY_BOARD_DIR}/board.md _UCORE_BOARD_INFO)
        # Re-run cmake when board.md changes so the frozen content stays
        # in sync. (configure_file already tracks ucore.py.in itself.)
        set_property(DIRECTORY APPEND PROPERTY
            CMAKE_CONFIGURE_DEPENDS ${MICROPY_BOARD_DIR}/board.md)
    else()
        set(_UCORE_BOARD_INFO "(no board.md provided for ${MICROPY_BOARD})")
    endif()
    configure_file(
        ${_UCORE_COMPONENT_DIR}/ucore.py.in
        ${CMAKE_BINARY_DIR}/ucore.py
        @ONLY)

    # Wrap the selected MICROPY_FROZEN_MANIFEST so our generated ucore.py
    # is included as a frozen module on top of whatever the board / port
    # default already freezes.
    file(WRITE ${CMAKE_BINARY_DIR}/ucore_manifest.py
"include(\"${MICROPY_FROZEN_MANIFEST}\")
module(\"ucore.py\", base_path=\"${CMAKE_BINARY_DIR}\")
")
    set(MICROPY_FROZEN_MANIFEST ${CMAKE_BINARY_DIR}/ucore_manifest.py)
endmacro()
