# Golden-trajectory regression test (standalone, headless — no Catch2).
add_executable(tfv_golden_test golden_trajectory.cpp)

target_include_directories(tfv_golden_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GLM_INCLUDE_DIRS}
)

# Links the engine library; it pulls in SDL/ImGui symbols even though the test
# never renders, so the SDL libs (and Apple frameworks) must be linked too.
target_link_libraries(tfv_golden_test PRIVATE
    trafficflowviz_lib
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
)

if(APPLE)
    target_link_libraries(tfv_golden_test PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
    )
endif()

add_test(NAME golden_trajectory COMMAND tfv_golden_test)
