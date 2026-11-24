cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.1.0")
set(PROJECT_VER_NUMBER 1)
project(disp32h)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)

# Omit the optional date suffix if PlatformIO mishandles its quoted spaces.
idf_component_get_property(ot_library openthread COMPONENT_LIB)
get_target_property(ot_definitions ${ot_library} COMPILE_DEFINITIONS)
list(FILTER ot_definitions EXCLUDE REGEX "^OPENTHREAD_BUILD_DATETIME=")
set_property(TARGET ${ot_library} PROPERTY COMPILE_DEFINITIONS "${ot_definitions}")
