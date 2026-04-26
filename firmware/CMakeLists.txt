cmake_minimum_required(VERSION 3.12)

include(${CMAKE_CURRENT_LIST_DIR}/components/ucore/project.cmake)
ucore_setup()

# Pulled in by main/main.c. Will be dropped in a follow-up cleanup once
# the unused include and example_connect() call there are removed.
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ucore)
