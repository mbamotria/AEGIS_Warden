cmake_minimum_required(VERSION 3.16)

# Point to esp-skainet components
set(EXTRA_COMPONENT_DIRS
    $ENV{HOME}/esp/esp-skainet/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aegis_warden)
