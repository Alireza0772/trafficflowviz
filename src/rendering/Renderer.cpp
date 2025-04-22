#include "rendering/Renderer.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
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

    // Implementation of anti-aliased line using Wu's algorithm
    void SDLRenderer::drawAALine(int x1, int y1, int x2, int y2)
    {
        // Get current draw color
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(m_sdlRenderer, &r, &g, &b, &a);

        // Wu's line algorithm
        bool steep = abs(y2 - y1) > abs(x2 - x1);
        if(steep)
        {
            std::swap(x1, y1);
            std::swap(x2, y2);
        }
        if(x1 > x2)
        {
            std::swap(x1, x2);
            std::swap(y1, y2);
        }

        int dx = x2 - x1;
        int dy = y2 - y1;
        float gradient = (dx == 0) ? 1.0f : (float)dy / dx;

        // Handle first endpoint
        float xend = round(x1);
        float yend = y1 + gradient * (xend - x1);
        float xgap = 1.0f - fmod(x1 + 0.5f, 1.0f);
        int xpxl1 = (int)xend;
        int ypxl1 = (int)yend;

        if(steep)
        {
            plotPixel(ypxl1, xpxl1, r, g, b, (1.0f - fmod(yend, 1.0f)) * xgap * a);
            plotPixel(ypxl1 + 1, xpxl1, r, g, b, fmod(yend, 1.0f) * xgap * a);
        }
        else
        {
            plotPixel(xpxl1, ypxl1, r, g, b, (1.0f - fmod(yend, 1.0f)) * xgap * a);
            plotPixel(xpxl1, ypxl1 + 1, r, g, b, fmod(yend, 1.0f) * xgap * a);
        }

        float intery = yend + gradient;

        // Handle second endpoint
        xend = round(x2);
        yend = y2 + gradient * (xend - x2);
        xgap = fmod(x2 + 0.5f, 1.0f);
        int xpxl2 = (int)xend;
        int ypxl2 = (int)yend;

        if(steep)
        {
            plotPixel(ypxl2, xpxl2, r, g, b, (1.0f - fmod(yend, 1.0f)) * xgap * a);
            plotPixel(ypxl2 + 1, xpxl2, r, g, b, fmod(yend, 1.0f) * xgap * a);
        }
        else
        {
            plotPixel(xpxl2, ypxl2, r, g, b, (1.0f - fmod(yend, 1.0f)) * xgap * a);
            plotPixel(xpxl2, ypxl2 + 1, r, g, b, fmod(yend, 1.0f) * xgap * a);
        }

        // Main loop
        if(steep)
        {
            for(int x = xpxl1 + 1; x < xpxl2; x++)
            {
                plotPixel((int)intery, x, r, g, b, (1.0f - fmod(intery, 1.0f)) * a);
                plotPixel((int)intery + 1, x, r, g, b, fmod(intery, 1.0f) * a);
                intery += gradient;
            }
        }
        else
        {
            for(int x = xpxl1 + 1; x < xpxl2; x++)
            {
                plotPixel(x, (int)intery, r, g, b, (1.0f - fmod(intery, 1.0f)) * a);
                plotPixel(x, (int)intery + 1, r, g, b, fmod(intery, 1.0f) * a);
                intery += gradient;
            }
        }
    }

    void SDLRenderer::drawAALine(int x1, int y1, int x2, int y2, int width)
    {
        // For wider anti-aliased lines, we'll draw multiple parallel lines
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

        // Draw multiple parallel anti-aliased lines
        for(int i = -hw; i <= hw; i++)
        {
            int ox1 = static_cast<int>(x1 + i * vx);
            int oy1 = static_cast<int>(y1 + i * vy);
            int ox2 = static_cast<int>(x2 + i * vx);
            int oy2 = static_cast<int>(y2 + i * vy);
            drawAALine(ox1, oy1, ox2, oy2);
        }
    }

    // Helper method to plot a pixel with transparency
    void SDLRenderer::plotPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
    {
        if(a < 255)
        {
            // Set alpha for blending
            SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_sdlRenderer, r, g, b, a);
            SDL_RenderDrawPoint(m_sdlRenderer, x, y);
            // Reset alpha
            SDL_SetRenderDrawColor(m_sdlRenderer, r, g, b, 255);
        }
        else
        {
            SDL_RenderDrawPoint(m_sdlRenderer, x, y);
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
    void MetalRenderer::drawAALine(int, int, int, int)
    { /* TODO: MetalKit draw anti-aliased line */
    }
    void MetalRenderer::drawAALine(int, int, int, int, int)
    { /* TODO: MetalKit draw anti-aliased line with width */
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
