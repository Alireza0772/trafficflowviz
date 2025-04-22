#ifndef TFV_SCENE_RENDERER_HPP
#define TFV_SCENE_RENDERER_HPP

#include <cmath>

#include "core/RoadNetwork.hpp"
#include "core/Simulation.hpp"
#include "rendering/Renderer.hpp"

namespace tfv
{

    class RoadRenderer
    {
      public:
        RoadRenderer(IRenderer* renderer, int panX, int panY, float scale,
                     bool antiAliasing = false)
            : m_r(renderer), m_panX(panX), m_panY(panY), m_scale(scale), roadWidth{10.f},
              dashed{false}, m_antiAliasing{antiAliasing}
        {
            // Apply anti-aliasing setting to the renderer
            if(m_r)
            {
                m_r->setAntiAliasing(antiAliasing);
            }
        }
        void draw(const RoadNetwork* net);
        void setAntiAliasing(bool enable) { m_antiAliasing = enable; }

      private:
        IRenderer* m_r;
        int m_panX, m_panY;
        float m_scale;
        float roadWidth{10.f};
        bool dashed{false};
        bool m_antiAliasing{false};
        void drawDashedLine(int x1, int y1, int x2, int y2);
    };

    class VehicleRenderer
    {
      public:
        VehicleRenderer(IRenderer* renderer, int panX, int panY, float scale,
                        bool antiAliasing = false);
        void draw(const VehicleMap& vehicles, const RoadNetwork* net);
        void setAntiAliasing(bool enable) { m_antiAliasing = enable; }

      private:
        IRenderer* m_r;
        int m_panX, m_panY;
        float m_scale;
        bool m_antiAliasing{false};
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

        /** Update method for animations */
        void update(double dt);

        /** Combined render method */
        void render();

        /** Enable/disable anti-aliased drawing */
        void setAntiAliasing(bool enable)
        {
            m_antiAliasing = enable;
            if(m_r)
            {
                m_r->setAntiAliasing(enable);
            }
        }

        float getZoom() const { return m_scale; }
        int getPanX() const { return m_panX; }
        int getPanY() const { return m_panY; }
        bool getAntiAliasing() const { return m_antiAliasing; }
        const RoadNetwork* getNetwork() const { return m_net; }

      private:
        IRenderer* m_r;
        const RoadNetwork* m_net{nullptr};
        float m_scale{1.0f};
        int m_panX{0};
        int m_panY{0};
        bool m_antiAliasing{true};

        // Store the last simulation snapshot
        VehicleMap m_lastSnapshot;
    };

} // namespace tfv
#endif