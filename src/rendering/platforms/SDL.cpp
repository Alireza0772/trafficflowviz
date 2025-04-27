#include "rendering/platforms/SDL.hpp"

#include <iostream>
namespace tfv
{

    // SDL implementations
    SDLRenderer::SDLRenderer(SDL_Window* window)
        : m_window(window), m_renderer(nullptr), m_antiAliasingEnabled(false)
    {
    }

    bool SDLRenderer::initialize()
    {

        // Initialize SDL_ttf
        if(TTF_Init() == -1)
        {
            std::cerr << "SDL_ttf could not initialize! SDL_ttf Error: " << TTF_GetError()
                      << std::endl;
            return false;
        }

        // Create renderer with accelerated and vsync flags for better quality
        m_renderer =
            SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if(!m_renderer)
        {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
            return false;
        }

        // Enable blending for transparency
        SDL_SetRenderDrawBlendMode(static_cast<SDL_Renderer*>(m_renderer), SDL_BLENDMODE_BLEND);

        // Set render quality hints
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

        return true;
    }

    void SDLRenderer::shutdown()
    {
        if(m_renderer)
        {
            SDL_DestroyRenderer(static_cast<SDL_Renderer*>(m_renderer));
            m_renderer = nullptr;
        }

        // Note: We don't own the window, just the renderer
        m_window = nullptr;

        TTF_Quit();
    }

    SDLRenderer::~SDLRenderer()
    {
        shutdown();
    }

    void* SDLRenderer::getNativeRenderer() const
    {
        return m_renderer;
    }

    void SDLRenderer::getWindowSize(int& width, int& height) const
    {
        if(m_window)
        {
            SDL_GetWindowSize(m_window, &width, &height);
        }
        else
        {
            width = 0;
            height = 0;
        }
    }

    void SDLRenderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
        SDL_RenderClear(m_renderer);
    }

    void SDLRenderer::present()
    {
        SDL_RenderPresent(m_renderer);
    }

    void SDLRenderer::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    }

    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2)
    {
        // Use SDL's built-in line drawing
        SDL_RenderDrawLine(m_renderer, x1, y1, x2, y2);
    }

    void SDLRenderer::drawLine(int x1, int y1, int x2, int y2, int width)
    {
        // Fast path for single‑pixel width
        if(width <= 1)
        {
            SDL_RenderDrawLine(m_renderer, x1, y1, x2, y2);
            return;
        }

#if SDL_VERSION_ATLEAST(2, 0, 18)
        // --- Hardware quad via SDL_RenderGeometry --------------------------
        float dx = static_cast<float>(x2 - x1);
        float dy = static_cast<float>(y2 - y1);
        float len = std::hypot(dx, dy);
        if(len < 0.001f)
        {
            SDL_Rect r{x1 - width / 2, y1 - width / 2, width, width};
            SDL_RenderFillRect(m_renderer, &r);
            return;
        }

        float ux = dx / len;
        float uy = dy / len;
        float px = -uy * width * 0.5f;
        float py = ux * width * 0.5f;

        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);
        SDL_Color col{r, g, b, a};

        SDL_Vertex verts[4]{
            {{static_cast<float>(x1) + px, static_cast<float>(y1) + py}, col, {0, 0}},
            {{static_cast<float>(x1) - px, static_cast<float>(y1) - py}, col, {0, 0}},
            {{static_cast<float>(x2) - px, static_cast<float>(y2) - py}, col, {0, 0}},
            {{static_cast<float>(x2) + px, static_cast<float>(y2) + py}, col, {0, 0}}};
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts, 4, idx, 6);
#else
        // --- Fallback: draw parallel lines ---------------------------------
        float dx = static_cast<float>(x2 - x1);
        float dy = static_cast<float>(y2 - y1);
        float len = std::hypot(dx, dy);
        if(len < 0.001f)
            return;

        float ux = dx / len;
        float uy = dy / len;
        float px = -uy;
        float py = ux;
        int half = width / 2;

        for(int i = -half; i <= half; ++i)
        {
            int ox = static_cast<int>(px * i);
            int oy = static_cast<int>(py * i);
            SDL_RenderDrawLine(m_renderer, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
        }
#endif
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
        SDL_RenderDrawPoint(m_renderer, x, y);
    }

    void SDLRenderer::drawRect(int x, int y, int w, int h)
    {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderDrawRect(m_renderer, &rect);
    }

    void SDLRenderer::fillRect(int x, int y, int w, int h)
    {
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderFillRect(m_renderer, &rect);
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
        SDL_RenderCopy(m_renderer, texture, nullptr, &dest);

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
        SDL_GetRenderDrawColor(m_renderer, &r, &g, &b, &a);
        SDL_Color color = {r, g, b, a};

        // Render text to surface
        SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
        if(!surface)
        {
            TTF_CloseFont(font);
            return nullptr;
        }

        // Convert surface to texture
        SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);

        // Clean up
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);

        return texture;
    }
} // namespace tfv
