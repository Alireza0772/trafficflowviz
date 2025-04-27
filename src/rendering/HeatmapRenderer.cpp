#include "rendering/HeatmapRenderer.hpp"
#include <algorithm>
#include <cmath>

namespace tfv
{
    HeatmapRenderer::HeatmapRenderer(Renderer* renderer) : m_renderer(renderer) {}

    void HeatmapRenderer::draw(const RoadNetwork* roadNetwork,
                               const std::unordered_map<uint32_t, float>& congestionLevels,
                               int panX, int panY, float scale)
    {
        if(!roadNetwork || !m_renderer)
            return;

        // Draw each road segment with color based on congestion
        for(const auto& visual : roadNetwork->segments())
        {
            // Get congestion level for this segment
            auto it = congestionLevels.find(visual.id);
            if(it == congestionLevels.end())
                continue;

            float congestionLevel = it->second;

            // Calculate road coordinates with pan and zoom
            int x1 = static_cast<int>(visual.x1 * scale) + panX;
            int y1 = static_cast<int>(visual.y1 * scale) + panY;
            int x2 = static_cast<int>(visual.x2 * scale) + panX;
            int y2 = static_cast<int>(visual.y2 * scale) + panY;

            // Get color based on congestion level
            Color color = getColorForCongestion(congestionLevel);

            // Apply opacity
            color.a = static_cast<uint8_t>(m_opacity * 255);

            // Draw the heatmap line
            float roadWidth =
                10.0f * scale * m_lineWidthFactor; // Use same width as road but scaled
            m_renderer->setColor(color.r, color.g, color.b, color.a);

            // Use anti-aliased lines for better quality (drawLine now handles anti-aliasing)
            m_renderer->drawLine(x1, y1, x2, y2, static_cast<int>(roadWidth));
        }
    }

    void HeatmapRenderer::setColorScheme(const Color& lowColor, const Color& mediumColor,
                                         const Color& highColor)
    {
        m_lowColor = lowColor;
        m_mediumColor = mediumColor;
        m_highColor = highColor;
    }

    Color HeatmapRenderer::getColorForCongestion(float level) const
    {
        // Clamp level to 0.0-1.0 manually
        level = std::max(0.0f, std::min(1.0f, level));

        Color result;

        if(level < 0.5f)
        {
            // Interpolate between low and medium colors
            float t = level / 0.5f;
            result.r = static_cast<uint8_t>(m_lowColor.r + t * (m_mediumColor.r - m_lowColor.r));
            result.g = static_cast<uint8_t>(m_lowColor.g + t * (m_mediumColor.g - m_lowColor.g));
            result.b = static_cast<uint8_t>(m_lowColor.b + t * (m_mediumColor.b - m_lowColor.b));
            result.a = 255;
        }
        else
        {
            // Interpolate between medium and high colors
            float t = (level - 0.5f) / 0.5f;
            result.r =
                static_cast<uint8_t>(m_mediumColor.r + t * (m_highColor.r - m_mediumColor.r));
            result.g =
                static_cast<uint8_t>(m_mediumColor.g + t * (m_highColor.g - m_mediumColor.g));
            result.b =
                static_cast<uint8_t>(m_mediumColor.b + t * (m_highColor.b - m_mediumColor.b));
            result.a = 255;
        }

        return result;
    }
} // namespace tfv