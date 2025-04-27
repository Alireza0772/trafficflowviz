find_package(Python3 REQUIRED COMPONENTS Development Interpreter)
find_package(pybind11 REQUIRED)

pybind11_add_module(trafficflowviz_py py_module.cpp)

target_include_directories(trafficflowviz_py PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GLM_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

target_link_libraries(trafficflowviz_py PRIVATE
    trafficflowviz_lib
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

set_target_properties(trafficflowviz_py
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    OUTPUT_NAME "trafficflowviz"
)

install(TARGETS trafficflowviz_py
    LIBRARY DESTINATION lib/python
)
