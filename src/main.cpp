#include "core/Engine.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
    // Create the engine
    tfv::Engine engine;

    // Initialize the engine with configuration
    if (!engine.initialize(argc, argv)) {
        std::cerr << "Failed to initialize TrafficFlowViz engine" << std::endl;
        return 1;
    }

    // Run the simulation
    engine.run();

    return 0;
}
