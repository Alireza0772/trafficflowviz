pybind11_add_module(trafficviz_py MODULE py_module.cpp)
set_target_properties(trafficviz_py PROPERTIES
    OUTPUT_NAME trafficviz
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python
    INSTALL_RPATH "@loader_path")
target_link_libraries(trafficviz_py PRIVATE tfv_engine)
