#ifndef TFV_HEATMAP_RENDERER_HPP
#define TFV_HEATMAP_RENDERER_HPP

#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "rendering/Renderer.hpp"

namespace tfv
{
    // Renderer-agnostic color structure
    struct Color
    {
        uint8_t r, g, b, a;
        Color(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0, uint8_t alpha = 255)
            : r(red), g(green), b(blue), a(alpha)
        {
        }
    };

    /**
     * Renderer for traffic heatmaps overlaid on the road network.
     * Uses color gradients to visualize congestion levels.
     */
    class HeatmapRenderer
    {
      public:
        explicit HeatmapRenderer(IRenderer* renderer);

        /**
         * Draw heatmap overlay on the road network
         * @param roadNetwork The road network to visualize
         * @param congestionLevels Map of segment IDs to congestion levels (0.0-1.0)
         * @param panX X pan offset
         * @param panY Y pan offset
         * @param scale Zoom scale
         */
        void draw(const RoadNetwork* roadNetwork,
                  const std::unordered_map<uint32_t, float>& congestionLevels, int panX, int panY,
                  float scale);

        /**
         * Set the color scheme for the heatmap
         * @param lowColor Color for low congestion (RGB)
         * @param mediumColor Color for medium congestion (RGB)
         * @param highColor Color for high congestion (RGB)
         */
        void setColorScheme(const Color& lowColor, const Color& mediumColor,
                            const Color& highColor);

        /**
         * Set heatmap opacity (0.0-1.0)
         */
        void setOpacity(float opacity) { m_opacity = std::max(0.0f, std::min(1.0f, opacity)); }

        /**
         * Set heatmap line width relative to road width
         */
        void setLineWidthFactor(float factor) { m_lineWidthFactor = std::max(0.1f, factor); }

      private:
        IRenderer* m_renderer;
        Color m_lowColor{0, 255, 0, 255};      // Green for low congestion
        Color m_mediumColor{255, 255, 0, 255}; // Yellow for medium congestion
        Color m_highColor{255, 0, 0, 255};     // Red for high congestion
        float m_opacity{0.7f};
        float m_lineWidthFactor{0.8f};

        /**
         * Interpolate between colors based on congestion level
         */
        Color getColorForCongestion(float level) const;
    };
} // namespace tfv

#endif