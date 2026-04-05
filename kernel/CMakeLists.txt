cmake_minimum_required(VERSION 3.12)

#set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/../micropython/ports/esp32)
set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/lib/micropython/ports/esp32)

# Set the board if it's not already set.
if(NOT MICROPY_BOARD)
    set(MICROPY_BOARD ESP32_GENERIC)
endif()

set(MICROPY_BOARD_DIR ${MICROPY_PORT_DIR}/boards/${MICROPY_BOARD})
# Set the board directory and check that it exists.
if(NOT MICROPY_BOARD_DIR)
    set(MICROPY_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/boards/${MICROPY_BOARD})
endif()
if(NOT EXISTS ${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
    message(FATAL_ERROR "Invalid MICROPY_BOARD specified: ${MICROPY_BOARD}")
endif()

# If a board variant is specified, check that it exists.
if(MICROPY_BOARD_VARIANT)
    if(NOT EXISTS ${MICROPY_BOARD_DIR}/mpconfigvariant_${MICROPY_BOARD_VARIANT}.cmake)
        message(FATAL_ERROR "Invalid MICROPY_BOARD_VARIANT specified: ${MICROPY_BOARD_VARIANT}")
    endif()  
endif()

# Define the output sdkconfig so it goes in the build directory.
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)

# Save the manifest file set from the cmake command line.
set(MICROPY_USER_FROZEN_MANIFEST ${MICROPY_FROZEN_MANIFEST})

# Include board config; this is expected to set (among other options):
# - SDKCONFIG_DEFAULTS
# - IDF_TARGET
include(${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
if(NOT MICROPY_BOARD_VARIANT)
    include(${MICROPY_BOARD_DIR}/mpconfigvariant.cmake OPTIONAL)
else()
    include(${MICROPY_BOARD_DIR}/mpconfigvariant_${MICROPY_BOARD_VARIANT}.cmake)
endif()

# Set the frozen manifest file. Note if MICROPY_FROZEN_MANIFEST is set from the cmake
# command line, then it will override the default and any manifest set by the board.
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

# pull in project components dependencies
set(EXTRA_COMPONENT_DIRS 
    "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common"
    #"${CMAKE_CURRENT_LIST_DIR}/ucore"
    #"${CMAKE_CURRENT_LIST_DIR}"
	)

# we are building micropython as a component so set this variable. it basically excludes the main.c from being built

set(MICROPY_COMPONENT_BUILD ON)
# pull in idf main cmake file
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# pull in the micropython runtime sources.
# list(APPEND EXTRA_COMPONENT_DIRS ${MICROPY_PORT_DIR}/main_${IDF_TARGET})
# the above line does not work anymore (and not needed for the current configuration). 
# micropython now expects one to encasulate micropython in a component for out of source builds 
# by including the esp32_common.cmake. see the ports/esp32/main for how its done. 
# the mpyruntime now encapsulates it and available in /components so idf can find it automatically



# start the first part of build system (discovery)
project(ucore)

# At this point, all component CMakeLists.txt files have been processed
# micropython will be built as an idf component into a library. 
# instead of modifying the py/mpconfig.h and esp32/mpconfigport.h directly, we hook into the micropython
# component by retriveing the COMPONENT_LIB(that is the component binary target) set by the idf build system
# and force including the overriding headers.

# for some reason
#   target_compile_options(${MICROPY_TARGET} PRIVATE
#   -include${CMAKE_CURRENT_LIST_DIR}/ucore/mpyoverrides.h
#   -include${CMAKE_CURRENT_LIST_DIR}/ucore/logging.h
#   )
#in the build command, it leads to 
#-Include mpyoverrides logging.h 
#which makes build fail, instead of 
#-Include mpyoverrides -Include logging.h 
#enclosing in quotes as done below makes it behave properly. 

# this line injects the overridden values into the micropython sources
# mpyruntime is micropythons component wrapper
#idf_component_get_property(MICROPY_TARGET main_${IDF_TARGET} COMPONENT_LIB)
idf_component_get_property(MICROPY_TARGET mpyruntime COMPONENT_LIB)

if(NOT ("${MICROPY_TARGET}" STREQUAL ""))
    message(STATUS "Found MicroPython target: ${MICROPY_TARGET}")
    
    target_compile_options(${MICROPY_TARGET} PRIVATE
    "-include${CMAKE_CURRENT_LIST_DIR}/components/ucore/include/ucore/mpy_overrides.h"
    "-include${CMAKE_CURRENT_LIST_DIR}/components/ucore/include/ucore/mpy_bindings.h"
    #"-include${CMAKE_CURRENT_LIST_DIR}/ucore/logging.h"
    )

    # we are allowed to do this becausee the includes are already in our search path
    #target_compile_options(${MICROPY_TARGET} PRIVATE
    #"-includeucore/mpy_overrides.h"
    #)



else()
    message(FATAL_ERROR "MicroPython target not found, build aborted")
endif()

#format the fatfs partition and add the sysroot image
#fatfs_create_spiflash_image(fatfs ./sysroot FLASH_IN_PROJECT)