set(TRAFFICFLOWVIZ_SOURCES
    # Core component sources
    core/Engine.cpp
    core/Configuration.cpp
    core/Simulation.cpp
    core/World.cpp
    core/RoadNetwork.cpp

    # Agent decision layer
    agents/RuleBasedBrain.cpp
    agents/BrainRegistry.cpp
    agents/ActionValidator.cpp
    agents/VtableBrain.cpp
    agents/NNBrain.cpp
    agents/DllBrain.cpp
    agents/PythonBrain.cpp
    agents/OnnxBrain.cpp

    # Control
    control/LightController.cpp

    # Perception (Phase 5)
    perception/SpatialIndex.cpp
    perception/PerceptionSystem.cpp

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

    # Observability / export (Phase 7)
    io/RunManifest.cpp
    io/TrajectoryExporter.cpp
    io/MetricsExporter.cpp

    # Network sources
    network/LiveFeed.cpp

    # Alerts
    alerts/AlertManager.cpp

    # Recording
    recording/RecordingManager.cpp
)
# NOTE: main.cpp is intentionally NOT in the library — it is compiled directly
# into the trafficviz executable below. Keeping main() out of the static lib lets
# test executables link the lib without a duplicate-main symbol clash.

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

# Embedded-Python brain backend (opt-in). PythonBrain.cpp is always compiled (a
# dependency-free stub when OFF); enabling links an embedded CPython interpreter.
if(TFV_WITH_PYTHON)
    find_package(Python3 REQUIRED COMPONENTS Development.Embed Interpreter)
    find_package(pybind11 REQUIRED)
    target_link_libraries(trafficflowviz_lib PUBLIC pybind11::embed)
    target_compile_definitions(trafficflowviz_lib PUBLIC TFV_WITH_PYTHON)
    message(STATUS "Embedded Python: linking pybind11::embed (Python3 ${Python3_VERSION})")
endif()

# ONNX Runtime brain backend (opt-in). OnnxBrain.cpp is always compiled (a
# dependency-free stub when OFF); enabling links ONNX Runtime. Try the vendor CMake
# config first (vcpkg / distributions), then fall back to a raw header+lib find.
if(TFV_WITH_ONNX)
    # NB: the case difference is deliberate — "onnxruntime" is the vendor CONFIG package
    # (vcpkg/official), "ONNXRuntime" maps to cmake/FindONNXRuntime.cmake. Do not normalize.
    find_package(onnxruntime CONFIG QUIET)
    if(onnxruntime_FOUND)
        target_link_libraries(trafficflowviz_lib PUBLIC onnxruntime::onnxruntime)
        message(STATUS "ONNX Runtime: onnxruntime::onnxruntime (CONFIG)")
    else()
        find_package(ONNXRuntime REQUIRED) # cmake/FindONNXRuntime.cmake (clean FATAL if missing)
        target_link_libraries(trafficflowviz_lib PUBLIC ONNXRuntime::ONNXRuntime)
        message(STATUS "ONNX Runtime: ONNXRuntime::ONNXRuntime (module find)")
    endif()
    target_compile_definitions(trafficflowviz_lib PUBLIC TFV_WITH_ONNX)
endif()

# Print include directories for this target
get_target_property(include_dirs trafficflowviz_lib INCLUDE_DIRECTORIES)
foreach(dir ${include_dirs})
  message(STATUS "trafficflowviz_lib include directory: ${dir}")
endforeach()

target_link_libraries(trafficflowviz_lib PUBLIC
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${CMAKE_DL_LIBS}   # dlopen/dlsym for the DLL brain backend
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

# Example external brain plugin (a reference tfv_brain.h implementation), built as a
# loadable module so it can be dlopen'd via sim.default_brain=dll:<path-to-.so>.
add_library(tfv_example_brain MODULE ${CMAKE_SOURCE_DIR}/examples/brains/example_brain.c)
target_include_directories(tfv_example_brain PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(tfv_example_brain PROPERTIES C_VISIBILITY_PRESET hidden)

# Headless batch runner (no SDL window): runs the sim and exports manifest + CSVs.
# Links the lib, which pulls SDL/Apple symbols even though no window is opened.
add_executable(tfv_headless tools/headless_runner.cpp)
target_compile_definitions(tfv_headless PRIVATE TFV_GIT_SHA="${TFV_GIT_SHA}")
target_link_libraries(tfv_headless PRIVATE
    trafficflowviz_lib
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)
if(APPLE)
    target_link_libraries(tfv_headless PRIVATE
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