#ifndef TFV_SCENE_RENDERER_HPP
#define TFV_SCENE_RENDERER_HPP

#include <cmath>

#include "Renderer.hpp"
#include "RoadNetwork.hpp"
#include "Simulation.hpp"

namespace tfv
{

    class RoadRenderer
    {
      public:
        RoadRenderer(IRenderer* renderer, int panX, int panY, float scale)
            : m_r(renderer), m_panX(panX), m_panY(panY), m_scale(scale), roadWidth{10.f},
              dashed{false}
        {
        }
        void draw(const RoadNetwork* net);

      private:
        IRenderer* m_r;
        int m_panX, m_panY;
        float m_scale;
        float roadWidth{10.f};
        bool dashed{false};
        void drawDashedLine(int x1, int y1, int x2, int y2);
    };

    class VehicleRenderer
    {
      public:
        VehicleRenderer(IRenderer* renderer, int panX, int panY, float scale);
        void draw(const VehicleMap& vehicles, const RoadNetwork* net);

      private:
        IRenderer* m_r;
        int m_panX, m_panY;
        float m_scale;
    };

    /** Immediate‑mode renderer for roads + vehicles (API-agnostic). */
    class SceneRenderer
    {
      public:
        explicit SceneRenderer(IRenderer* r) : m_r(r) {}

        /** Supply road network (can be nullptr). */
        void setNetwork(const RoadNetwork* net) { m_net = net; }

        /** Set zoom scale (1.0 = 100%). */
        void setZoom(float scale) { m_scale = scale; }
        /** Set pan offset in pixels. */
        void setPan(int dx, int dy)
        {
            m_panX = dx;
            m_panY = dy;
        }

        /** Draw roads first, then vehicles. */
        void draw(const VehicleMap& vehicles);

        float getZoom() const { return m_scale; }
        int getPanX() const { return m_panX; }
        int getPanY() const { return m_panY; }

      private:
        IRenderer* m_r;
        const RoadNetwork* m_net{nullptr};
        float m_scale{1.0f};
        int m_panX{0};
        int m_panY{0};
    };

} // namespace tfv
#endif