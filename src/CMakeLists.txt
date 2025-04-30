set(TRAFFICFLOWVIZ_SOURCES
    # Core component sources
    core/Engine.cpp
    core/Simulation.cpp
    core/RoadNetwork.cpp

    # Rendering component sources
    rendering/Renderer.cpp
    rendering/SceneRenderer.cpp
    rendering/HeatmapRenderer.cpp
    rendering/ImGuiRenderer.cpp

    # New layer system
    rendering/LayerStack.cpp
    rendering/layers/SimulationLayer.cpp
    rendering/layers/HeatmapLayer.cpp
    rendering/layers/ImGuiLayer.cpp

    # Platform-specific sources
    platforms/SDL/SDLRenderer.cpp
    platforms/SDL/SDLWindow.cpp
    platforms/Apple/MetalRenderer.mm

    # Utilities
    utils/LoggingManager.cpp

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

# ImGui sources
set(IMGUI_SOURCES
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_sdl2.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_sdlrenderer2.cpp
)

# Define library
add_library(trafficflowviz_lib STATIC ${TRAFFICFLOWVIZ_SOURCES} ${IMGUI_SOURCES})
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

# Add macOS-specific frameworks
if(APPLE)
    target_link_libraries(trafficflowviz_lib PUBLIC
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
    )
endif()

# Define executable
add_executable(trafficviz main.cpp)
target_link_libraries(trafficviz PRIVATE
    trafficflowviz_lib
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

# define target-specific macros
target_compile_definitions(trafficviz PRIVATE
    $<$<CONFIG:DEBUG>:DEBUG>
    $<$<CONFIG:RELEASE>:NDEBUG>
)

# Add macOS-specific frameworks for the executable
if(APPLE)
    target_link_libraries(trafficviz PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
    )
endif()

# Install targets
install(TARGETS trafficviz
        RUNTIME DESTINATION bin)

install(TARGETS trafficflowviz_lib
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)