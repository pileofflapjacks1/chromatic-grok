# Modified by pileofflapjacks1, 2026: project renamed to chromatic_grok.
# Original: ModRetro oss-chromatic-console-mcu v4.6 (63ed3b6), GPL-3.0.
cmake_minimum_required(VERSION 3.5)

add_definitions(-DLV_LVGL_H_INCLUDE_SIMPLE)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(chromatic_grok)
