cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components)
if(NOT SDKCONFIG_DEFAULTS)   # a -D on the command line (console-off, direct-off) wins
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../common/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.theme")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/lv_font_default.cmake)   # LVGL's default font (step 1, D2)
project(omni_watch)          # the name stays: the app descriptor and omni_watch.bin are unchanged
