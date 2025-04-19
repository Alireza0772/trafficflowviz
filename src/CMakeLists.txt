add_library(tfv_engine
    Engine.cpp Engine.hpp
    Renderer.cpp Renderer.hpp
    Simulation.cpp Simulation.hpp
    CSVLoader.cpp  CSVLoader.hpp
    LiveFeed.cpp   LiveFeed.hpp
    SceneRenderer.cpp  SceneRenderer.hpp
    RoadNetwork.cpp    RoadNetwork.hpp
    TrafficEntity.hpp
)

target_include_directories(tfv_engine
    PUBLIC ${TFV_SRC_DIR} ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(tfv_engine PUBLIC SDL2::SDL2)

add_executable(trafficviz main.cpp)
target_link_libraries(trafficviz PRIVATE tfv_engine)