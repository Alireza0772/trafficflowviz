set(TRAFFICFLOWVIZ_SOURCES
    # Core component sources
    core/Engine.cpp
    core/Simulation.cpp
    core/RoadNetwork.cpp

    # Rendering component sources
    rendering/Renderer.cpp
    rendering/SceneRenderer.cpp
    rendering/HeatmapRenderer.cpp

    # Data loading sources
    data/CSVLoader.cpp

    # Network sources
    network/LiveFeed.cpp

    # Alerts
    alerts/AlertManager.cpp

    # Recording
    recording/RecordingManager.cpp

    # Main application
    main.cpp
)

# Define library
add_library(trafficflowviz_lib STATIC ${TRAFFICFLOWVIZ_SOURCES})
target_include_directories(trafficflowviz_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${GLM_INCLUDE_DIRS}
)

# Print include directories for this target
get_target_property(include_dirs trafficflowviz_lib INCLUDE_DIRECTORIES)
foreach(dir ${include_dirs})
  message(STATUS "trafficflowviz_lib include directory: ${dir}")
endforeach()

target_link_libraries(trafficflowviz_lib PUBLIC
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

# Define executable
add_executable(trafficviz main.cpp)
target_link_libraries(trafficviz PRIVATE
    trafficflowviz_lib
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

# Install targets
install(TARGETS trafficviz
        RUNTIME DESTINATION bin)

install(TARGETS trafficflowviz_lib
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)