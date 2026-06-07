#ifndef TFV_SCENE_STYLE_HPP
#define TFV_SCENE_STYLE_HPP

#include "core/TrafficEntity.hpp"

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace tfv
{
    // How vehicles are colored in the scene. All modes read only per-vehicle render
    // state (no sim coupling). Acceleration is intentionally absent: Vehicle.acc is not
    // populated by the step path, so a by-accel mode would be uniformly dead.
    enum class ColorEncoding
    {
        Speed,
        Lane,
        VehicleClass
    };

    inline const char* encodingName(ColorEncoding e)
    {
        switch(e)
        {
        case ColorEncoding::Lane: return "Lane";
        case ColorEncoding::VehicleClass: return "Vehicle Class";
        case ColorEncoding::Speed: break;
        }
        return "Speed";
    }

    struct RGB8
    {
        uint8_t r, g, b;
    };

    // Scene theme: dark "studio" (default, for talks/screens) vs light "paper" (figures).
    enum class SceneTheme
    {
        StudioDark,
        PaperLight
    };
    inline const char* themeName(SceneTheme t)
    {
        return t == SceneTheme::PaperLight ? "Paper Light" : "Studio Dark";
    }
    struct ThemePalette
    {
        RGB8 bg, asphalt, edge, center;
    };
    inline ThemePalette themePalette(SceneTheme t)
    {
        if(t == SceneTheme::PaperLight)
            return {{236, 238, 242}, {206, 211, 217}, {140, 146, 154}, {170, 150, 60}};
        return {{24, 26, 32}, {52, 56, 64}, {96, 100, 110}, {210, 200, 120}};
    }

    namespace style_detail
    {
        inline RGB8 lerp8(RGB8 a, RGB8 b, float u)
        {
            auto L = [&](uint8_t x, uint8_t y) {
                return static_cast<uint8_t>(x + std::lround((static_cast<int>(y) - x) * u));
            };
            return {L(a.r, b.r), L(a.g, b.g), L(a.b, b.b)};
        }
    } // namespace style_detail

    // Speed -> green->amber->red gradient, normalized by the desired-speed cap.
    inline RGB8 speedRGB(float speed, float v0)
    {
        float t = speed / (v0 > 0.1f ? v0 : 13.9f);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const RGB8 lo{46, 204, 113}, mid{241, 196, 15}, hi{231, 76, 60};
        return t < 0.5f ? style_detail::lerp8(lo, mid, t / 0.5f)
                        : style_detail::lerp8(mid, hi, (t - 0.5f) / 0.5f);
    }

    // Distinct per-lane hues (wraps for >6 lanes).
    inline RGB8 laneRGB(uint8_t lane)
    {
        static const RGB8 P[] = {{52, 152, 219}, {46, 204, 113}, {241, 196, 15},
                                 {231, 76, 60},  {155, 89, 182}, {26, 188, 156}};
        return P[lane % (sizeof(P) / sizeof(P[0]))];
    }

    // Categorical by vehicle type string.
    inline RGB8 classRGB(const std::string& type)
    {
        if(type == "truck")
            return {155, 89, 182};
        if(type == "bus")
            return {26, 188, 156};
        if(type == "emergency")
            return {231, 76, 60};
        if(type == "car")
            return {52, 152, 219};
        return {149, 165, 166}; // unknown
    }

    inline RGB8 encodeVehicleColor(const Vehicle& v, ColorEncoding e, float v0)
    {
        switch(e)
        {
        case ColorEncoding::Lane: return laneRGB(v.laneIndex);
        case ColorEncoding::VehicleClass: return classRGB(v.type);
        case ColorEncoding::Speed: break;
        }
        return speedRGB(glm::length(v.vel), v0);
    }

} // namespace tfv

#endif // TFV_SCENE_STYLE_HPP
