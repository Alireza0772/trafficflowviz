#include "rendering/Renderer.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <iostream>

namespace tfv
{

    SDLRenderer::SDLRenderer(SDL_Renderer* sdlRenderer) : m_sdlRenderer(sdlRenderer)
    {
        // Initialize SDL_ttf
        if(TTF_Init() == -1)
        {
            std::cerr << "SDL_ttf could not initialize! SDL_ttf Error: " << TTF_GetError()
                      << std::endl;
        }
    }

    SDLRenderer::~SDLRenderer()
    {
        // No ownership of renderer, it's cleaned up by Engine
        TTF_Quit();
    }

    void SDLRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(m_sdlRenderer, r, g, b, a);
        SDL_RenderClear(m_sdlRenderer);
    }

    void SDLRenderer::present()
    {
        SDL_RenderPresent(m_sdlRenderer);
    }

    void SDLRenderer::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(m_sdlRenderer, r, g, b, a);
    }

    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2)
    {
        SDL_RenderDrawLine(m_sdlRenderer, x1, y1, x2, y2);
    }

    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2, int width)
    {
        // Simple technique to draw thick lines - draw multiple lines with offsets
        // Better solution would use SDL2_gfx library for thick lines
        int dx = x2 - x1;
        int dy = y2 - y1;
        double length = std::sqrt(dx * dx + dy * dy);

        if(length < 0.0001)
        {
            // Handle zero-length line
            return;
        }

        double ux = dx / length;
        double uy = dy / length;

        // Perpendicular unit vector
        double vx = -uy;
        double vy = ux;

        // Half-width
        int hw = width / 2;

        // Draw multiple parallel lines
        for(int i = -hw; i <= hw; i++)
        {
            int ox1 = static_cast<int>(x1 + i * vx);
            int oy1 = static_cast<int>(y1 + i * vy);
            int ox2 = static_cast<int>(x2 + i * vx);
            int oy2 = static_cast<int>(y2 + i * vy);
            SDL_RenderDrawLine(m_sdlRenderer, ox1, oy1, ox2, oy2);
        }
    }

    void SDLRenderer::drawPoint(int x, int y)
    {
        SDL_RenderDrawPoint(m_sdlRenderer, x, y);
    }

    void SDLRenderer::drawRect(int x, int y, int w, int h)
    {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderDrawRect(m_sdlRenderer, &rect);
    }

    void SDLRenderer::fillRect(int x, int y, int w, int h)
    {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(m_sdlRenderer, &rect);
    }

    void SDLRenderer::drawText(const std::string& text, int x, int y)
    {
        SDL_Texture* texture = createTextTexture(text);
        if(!texture)
        {
            return;
        }

        // Get texture dimensions
        int width, height;
        SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);

        // Render the text
        SDL_Rect dest = {x, y, width, height};
        SDL_RenderCopy(m_sdlRenderer, texture, nullptr, &dest);

        // Clean up
        SDL_DestroyTexture(texture);
    }

    SDL_Texture* SDLRenderer::createTextTexture(const std::string& text)
    {
        // Load a font (in a real implementation, we'd cache this)
        TTF_Font* font = TTF_OpenFont("/System/Library/Fonts/Supplemental/Arial.ttf", 16);
        if(!font)
        {
            std::cerr << "Failed to load font: " << TTF_GetError() << std::endl;
            // Try fallback font
            font = TTF_OpenFont("/System/Library/Fonts/Supplemental/Courier New.ttf", 16);
            if(!font)
            {
                return nullptr;
            }
        }

        // Get current draw color
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(m_sdlRenderer, &r, &g, &b, &a);
        SDL_Color color = {r, g, b, a};

        // Render text to surface
        SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
        if(!surface)
        {
            TTF_CloseFont(font);
            return nullptr;
        }

        // Convert surface to texture
        SDL_Texture* texture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);

        // Clean up
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);

        return texture;
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
    void MetalRenderer::setColor(uint8_t, uint8_t, uint8_t, uint8_t)
    { /* TODO: MetalKit set color */
    }
    void MetalRenderer::drawLine(int, int, int, int)
    { /* TODO: MetalKit draw line */
    }
    void MetalRenderer::drawLine(int, int, int, int, int)
    { /* TODO: MetalKit draw line */
    }
    void MetalRenderer::drawPoint(int, int)
    { /* TODO: MetalKit draw point */
    }
    void MetalRenderer::drawRect(int, int, int, int)
    { /* TODO: MetalKit draw rect */
    }
    void MetalRenderer::fillRect(int, int, int, int)
    { /* TODO: MetalKit fill rect */
    }
    void MetalRenderer::drawText(const std::string&, int, int)
    { /* TODO: MetalKit draw text */
    }

} // namespace tfv
