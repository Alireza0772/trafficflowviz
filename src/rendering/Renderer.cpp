#include "rendering/Renderer.hpp"
#include "rendering/platforms/Metal.hpp"
#include "rendering/platforms/SDL.hpp"

#include <cmath>
#include <iostream>

// Metal includes (only on Apple platforms)
#if defined(__APPLE__)
#ifdef __OBJC__
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#endif
#endif

namespace tfv
{
    // Factory method implementation
    std::unique_ptr<Renderer> Renderer::create(const std::string& type, void* window)
    {
        if(type == "SDL")
        {
            return std::make_unique<SDLRenderer>(static_cast<SDL_Window*>(window));
        }
        else if(type == "Metal")
        {
#if defined(__APPLE__)
            return std::make_unique<MetalRenderer>();
#else
            throw std::runtime_error("Metal renderer is only supported on Apple platforms");
#endif
        }
        else
        {
            throw std::runtime_error("Unsupported renderer type: " + type);
        }
    }

} // namespace tfv
