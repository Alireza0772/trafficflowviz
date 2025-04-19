cmake_minimum_required(VERSION 3.25)
project(TrafficFlowViz VERSION 0.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(TFV_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
option(TFV_ENABLE_PYTHON "Build Python bindings" ON)

find_package(SDL2 REQUIRED CONFIG)
find_package(pybind11 CONFIG QUIET)
if(TFV_ENABLE_PYTHON AND NOT pybind11_FOUND)
    message(FATAL_ERROR "pybind11 not found; brew install pybind11 or disable TFV_ENABLE_PYTHON")
endif()

add_subdirectory(src)
if(TFV_ENABLE_PYTHON)
    add_subdirectory(bindings)
endif()

install(DIRECTORY scripts DESTINATION share/trafficflowviz)
