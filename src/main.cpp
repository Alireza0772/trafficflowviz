#include "core/Engine.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
    // Create the engine with default SDL renderer
    tfv::Engine engine("Traffic Flow Visualization", 1280, 720, "SDL");

    // Initialize the engine and run the simulation
    engine.run();

    return 0;
}
