#include "Renderer.hpp"

#include <SDL.h>

namespace tfv
{

    SDLRenderer::SDLRenderer(void* sdlRenderer) : m_sdlRenderer(sdlRenderer) {}
    SDLRenderer::~SDLRenderer() {}
    void SDLRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(static_cast<SDL_Renderer*>(m_sdlRenderer), r, g, b, a);
        SDL_RenderClear(static_cast<SDL_Renderer*>(m_sdlRenderer));
    }
    void SDLRenderer::present()
    {
        SDL_RenderPresent(static_cast<SDL_Renderer*>(m_sdlRenderer));
    }
    void SDLRenderer::setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(static_cast<SDL_Renderer*>(m_sdlRenderer), r, g, b, a);
    }
    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2)
    {
        SDL_RenderDrawLine(static_cast<SDL_Renderer*>(m_sdlRenderer), x1, y1, x2, y2);
    }
    void SDLRenderer::drawPoint(int x, int y)
    {
        SDL_RenderDrawPoint(static_cast<SDL_Renderer*>(m_sdlRenderer), x, y);
    }

    // MetalRenderer stub implementation
    MetalRenderer::MetalRenderer(void* metalView) : m_metalView(metalView) {}
    MetalRenderer::~MetalRenderer() {}
    void MetalRenderer::clear(uint8_t, uint8_t, uint8_t, uint8_t)
    { /* TODO: MetalKit clear */
    }
    void MetalRenderer::present()
    { /* TODO: MetalKit present */
    }
    void MetalRenderer::setDrawColor(uint8_t, uint8_t, uint8_t, uint8_t)
    { /* TODO: MetalKit set color */
    }
    void MetalRenderer::drawLine(int, int, int, int)
    { /* TODO: MetalKit draw line */
    }
    void MetalRenderer::drawPoint(int, int)
    { /* TODO: MetalKit draw point */
    }

} // namespace tfv
