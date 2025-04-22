#include "rendering/Renderer.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <iostream>

namespace tfv
{
    // Factory method implementation
    std::unique_ptr<IRenderer> IRenderer::create(const std::string& type)
    {
        if(type == "SDL")
        {
            return std::make_unique<SDLRenderer>();
        }
        else if(type == "Metal")
        {
            return std::make_unique<MetalRenderer>();
        }
        else
        {
            throw std::runtime_error("Unsupported renderer type: " + type);
        }
    }

    // SDL implementations
    SDLRenderer::SDLRenderer() : m_sdlWindow(nullptr), m_sdlRenderer(nullptr) {}

    bool SDLRenderer::initialize(void* windowHandle)
    {
        m_sdlWindow = windowHandle;
        SDL_Window* window = static_cast<SDL_Window*>(windowHandle);

        // Initialize SDL_ttf
        if(TTF_Init() == -1)
        {
            std::cerr << "SDL_ttf could not initialize! SDL_ttf Error: " << TTF_GetError()
                      << std::endl;
            return false;
        }

        // Create renderer with accelerated and vsync flags for better quality
        m_sdlRenderer =
            SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if(!m_sdlRenderer)
        {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
            return false;
        }

        // Enable blending for transparency
        SDL_SetRenderDrawBlendMode(static_cast<SDL_Renderer*>(m_sdlRenderer), SDL_BLENDMODE_BLEND);

        // Set render quality hints
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

        return true;
    }

    void SDLRenderer::shutdown()
    {
        if(m_sdlRenderer)
        {
            SDL_DestroyRenderer(static_cast<SDL_Renderer*>(m_sdlRenderer));
            m_sdlRenderer = nullptr;
        }

        // Note: We don't own the window, just the renderer
        m_sdlWindow = nullptr;

        TTF_Quit();
    }

    SDLRenderer::~SDLRenderer()
    {
        shutdown();
    }

    void* SDLRenderer::getNativeRenderer() const
    {
        return m_sdlRenderer;
    }

    void SDLRenderer::getWindowSize(int& width, int& height) const
    {
        if(m_sdlWindow)
        {
            SDL_GetWindowSize(static_cast<SDL_Window*>(m_sdlWindow), &width, &height);
        }
        else
        {
            width = 0;
            height = 0;
        }
    }

    void SDLRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(static_cast<SDL_Renderer*>(m_sdlRenderer), r, g, b, a);
        SDL_RenderClear(static_cast<SDL_Renderer*>(m_sdlRenderer));
    }

    void SDLRenderer::present()
    {
        SDL_RenderPresent(static_cast<SDL_Renderer*>(m_sdlRenderer));
    }

    void SDLRenderer::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(static_cast<SDL_Renderer*>(m_sdlRenderer), r, g, b, a);
    }

    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2)
    {
        // Use SDL's built-in line drawing
        SDL_RenderDrawLine(static_cast<SDL_Renderer*>(m_sdlRenderer), x1, y1, x2, y2);
    }

    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2, int width)
    {
        // For thicker lines, we still need to draw multiple lines
        // SDL doesn't have a native thick line drawing function
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
            SDL_RenderDrawLine(static_cast<SDL_Renderer*>(m_sdlRenderer), ox1, oy1, ox2, oy2);
        }
    }

    void SDLRenderer::setAntiAliasing(bool enable)
    {
        m_antiAliasingEnabled = enable;
        // SDL doesn't have direct anti-aliasing control,
        // but we can use a hint for the renderer quality
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, enable ? "1" : "0");
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, enable ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    }

    void SDLRenderer::drawPoint(int x, int y)
    {
        SDL_RenderDrawPoint(static_cast<SDL_Renderer*>(m_sdlRenderer), x, y);
    }

    void SDLRenderer::drawRect(int x, int y, int w, int h)
    {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderDrawRect(static_cast<SDL_Renderer*>(m_sdlRenderer), &rect);
    }

    void SDLRenderer::fillRect(int x, int y, int w, int h)
    {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(static_cast<SDL_Renderer*>(m_sdlRenderer), &rect);
    }

    void SDLRenderer::drawText(const std::string& text, int x, int y)
    {
        SDL_Texture* texture = static_cast<SDL_Texture*>(createTextTexture(text));
        if(!texture)
        {
            return;
        }

        // Get texture dimensions
        int width, height;
        SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);

        // Render the text
        SDL_Rect dest = {x, y, width, height};
        SDL_RenderCopy(static_cast<SDL_Renderer*>(m_sdlRenderer), texture, nullptr, &dest);

        // Clean up
        SDL_DestroyTexture(texture);
    }

    void* SDLRenderer::createTextTexture(const std::string& text)
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
        SDL_GetRenderDrawColor(static_cast<SDL_Renderer*>(m_sdlRenderer), &r, &g, &b, &a);
        SDL_Color color = {r, g, b, a};

        // Render text to surface
        SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
        if(!surface)
        {
            TTF_CloseFont(font);
            return nullptr;
        }

        // Convert surface to texture
        SDL_Texture* texture =
            SDL_CreateTextureFromSurface(static_cast<SDL_Renderer*>(m_sdlRenderer), surface);

        // Clean up
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);

        return texture;
    }

    // MetalRenderer stub implementation
    MetalRenderer::MetalRenderer() : m_metalView(nullptr) {}

    bool MetalRenderer::initialize(void* windowHandle)
    {
        m_metalView = windowHandle;
        /* TODO: Initialize MetalKit renderer */
        return true;
    }

    void MetalRenderer::shutdown()
    {
        m_metalView = nullptr;
        /* TODO: Clean up MetalKit resources */
    }

    MetalRenderer::~MetalRenderer()
    {
        shutdown();
    }

    void* MetalRenderer::getNativeRenderer() const
    {
        return m_metalView;
    }

    void MetalRenderer::getWindowSize(int& width, int& height) const
    {
        /* TODO: Implement for MetalKit */
        width = 0;
        height = 0;
    }

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
    { /* TODO: MetalKit draw line with width */
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

    void MetalRenderer::setAntiAliasing(bool enable)
    {
        m_antiAliasingEnabled = enable;
        /* TODO: MetalKit anti-aliasing control */
    }

} // namespace tfv
