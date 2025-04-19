#ifndef TFV_ENGINE_HPP
#define TFV_ENGINE_HPP

#include <SDL.h>

#include <string>

#include "Renderer.hpp"
#include "RoadNetwork.hpp"
#include "SceneRenderer.hpp"
#include "Simulation.hpp"

namespace tfv
{

    class Engine
    {
      public:
        Engine(const std::string& title, int w, int h);
        ~Engine();

        /** Override default CSV path before init(). */
        void setCSV(std::string p) { m_csvPath = std::move(p); }

        bool init();
        void run();

      private:
        void handleEvents();
        void update(double dt);
        void render();

        // window / renderer
        std::string m_title;
        int m_w, m_h;
        SDL_Window* m_window{nullptr};
        tfv::IRenderer* m_renderer{nullptr};

        // timing
        bool m_running{false};
        double m_t{0.0};

        // core subsystems
        Simulation m_sim;
        SceneRenderer* m_scene{nullptr};
        RoadNetwork m_roads;

        // data paths
        std::string m_csvPath{"./vehicles_complex.csv"};
        std::string m_roadPath{"./roads_complex.csv"};
    };

} // namespace tfv
#endif