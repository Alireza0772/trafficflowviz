#include "core/Engine.hpp"
#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char* argv[])
{
    // Initialize SDL
    if(SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create the engine
    tfv::Engine engine("Traffic Flow Visualization", 1280, 720);

    // Initialize the engine
    if(!engine.init())
    {
        std::cerr << "Failed to initialize the engine" << std::endl;
        SDL_Quit();
        return 1;
    }

    // Run the simulation
    engine.run();

    // Clean up
    SDL_Quit();

    return 0;
}
