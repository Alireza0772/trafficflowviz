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

    // ---- Vector primitives: default implementations tessellated onto fillGeometry/fillQuad ----

    void Renderer::fillCircle(float cx, float cy, float rad, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t a, int seg)
    {
        if(seg < 3)
            seg = 3;
        if(rad < 0.5f)
            rad = 0.5f;
        std::vector<RVertex> v;
        v.reserve(static_cast<std::size_t>(seg + 1));
        v.push_back({cx, cy, r, g, b, a});
        for(int i = 0; i < seg; ++i)
        {
            const float t = 6.2831853f * static_cast<float>(i) / static_cast<float>(seg);
            v.push_back({cx + std::cos(t) * rad, cy + std::sin(t) * rad, r, g, b, a});
        }
        std::vector<int> idx;
        idx.reserve(static_cast<std::size_t>(seg * 3));
        for(int i = 0; i < seg; ++i)
        {
            idx.push_back(0);
            idx.push_back(i + 1);
            idx.push_back((i + 1) % seg + 1);
        }
        fillGeometry(v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
    }

    void Renderer::fillPolygon(const glm::vec2* p, int n, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        if(n < 3)
            return;
        std::vector<RVertex> v;
        v.reserve(static_cast<std::size_t>(n));
        for(int i = 0; i < n; ++i)
            v.push_back({p[i].x, p[i].y, r, g, b, a});
        std::vector<int> idx;
        idx.reserve(static_cast<std::size_t>((n - 2) * 3));
        for(int i = 1; i + 1 < n; ++i)
        {
            idx.push_back(0);
            idx.push_back(i);
            idx.push_back(i + 1);
        }
        fillGeometry(v.data(), n, idx.data(), static_cast<int>(idx.size()));
    }

    void Renderer::strokePolyline(const glm::vec2* p, int n, float width, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a, bool roundJoin, bool closed)
    {
        if(n < 2)
            return;
        const float h = width * 0.5f;
        const int spans = closed ? n : n - 1;
        for(int i = 0; i < spans; ++i)
        {
            const glm::vec2 p0 = p[i], p1 = p[(i + 1) % n];
            glm::vec2 d = p1 - p0;
            const float L = glm::length(d);
            if(L < 1e-4f)
                continue;
            d /= L;
            const glm::vec2 nrm(-d.y, d.x);
            const RVertex a0{p0.x + nrm.x * h, p0.y + nrm.y * h, r, g, b, a};
            const RVertex b0{p0.x - nrm.x * h, p0.y - nrm.y * h, r, g, b, a};
            const RVertex a1{p1.x + nrm.x * h, p1.y + nrm.y * h, r, g, b, a};
            const RVertex b1{p1.x - nrm.x * h, p1.y - nrm.y * h, r, g, b, a};
            fillQuad(a0, a1, b1, b0);
            if(roundJoin && h > 1.0f) // fill the joint so consecutive spans meet without a gap
                fillCircle(p1.x, p1.y, h, r, g, b, a, 10);
        }
    }

    void Renderer::strokeCircle(float cx, float cy, float rad, float width, uint8_t r, uint8_t g,
                                uint8_t b, uint8_t a, int seg)
    {
        if(seg < 3)
            seg = 3;
        std::vector<glm::vec2> pts;
        pts.reserve(static_cast<std::size_t>(seg));
        for(int i = 0; i < seg; ++i)
        {
            const float t = 6.2831853f * static_cast<float>(i) / static_cast<float>(seg);
            pts.push_back({cx + std::cos(t) * rad, cy + std::sin(t) * rad});
        }
        strokePolyline(pts.data(), static_cast<int>(pts.size()), width, r, g, b, a, false, true);
    }

    void Renderer::flattenQuadratic(glm::vec2 p0, glm::vec2 c, glm::vec2 p1, int steps,
                                    std::vector<glm::vec2>& out)
    {
        if(steps < 1)
            steps = 1;
        for(int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps), u = 1.0f - t;
            out.push_back(u * u * p0 + 2.0f * u * t * c + t * t * p1);
        }
    }

    void Renderer::flattenCubic(glm::vec2 p0, glm::vec2 c0, glm::vec2 c1, glm::vec2 p1, int steps,
                                std::vector<glm::vec2>& out)
    {
        if(steps < 1)
            steps = 1;
        for(int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps), u = 1.0f - t;
            out.push_back(u * u * u * p0 + 3.0f * u * u * t * c0 + 3.0f * u * t * t * c1 +
                          t * t * t * p1);
        }
    }

} // namespace tfv
