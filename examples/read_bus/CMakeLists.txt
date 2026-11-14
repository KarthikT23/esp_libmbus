cmake_minimum_required(VERSION 3.16)

# repo root is the component (dir name must stay esp_libmbus)
set(EXTRA_COMPONENT_DIRS "../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(read_bus)
