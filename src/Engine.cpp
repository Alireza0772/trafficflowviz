#include "Engine.hpp"

#include <chrono>
#include <cmath>
#include <iostream>

#include "CSVLoader.hpp"

namespace tfv
{

    Engine::Engine(const std::string& title, int w, int h) : m_title(title), m_w(w), m_h(h) {}

    Engine::~Engine()
    {
        if(m_scene)
            delete m_scene;
        if(m_renderer)
            delete m_renderer;
        if(m_window)
            SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    bool Engine::init()
    {
        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            std::cerr << "SDL init failed: " << SDL_GetError() << '\n';
            return false;
        }

        m_window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    m_w, m_h, 0);

        if(!m_window)
        {
            std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
            return false;
        }

        SDL_Renderer* sdlRenderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
        if(!sdlRenderer)
        {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
            return false;
        }

        m_renderer = new SDLRenderer(sdlRenderer);
        m_scene = new SceneRenderer(m_renderer);

        m_roads.loadCSV(m_roadPath);
        m_scene->setNetwork(&m_roads);

        auto vehicles = tfv::loadVehiclesCSV(m_csvPath);
        std::cout << "[CSV] loaded " << vehicles.size() << " vehicles\n";
        for(const auto& v : vehicles)
            m_sim.addVehicle(v);

        return true;
    }

    void Engine::run()
    {
        if(!init())
            return;

        using clk = std::chrono::high_resolution_clock;
        auto last = clk::now();
        m_running = true;

        while(m_running)
        {
            auto now = clk::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;

            handleEvents();
            update(dt);
            render();
        }
    }

    void Engine::handleEvents()
    {
        for(SDL_Event e; SDL_PollEvent(&e);)
        {
            if(e.type == SDL_QUIT)
                m_running = false;
            if(e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                m_running = false;
            // Pan with arrow keys
            if(e.type == SDL_KEYDOWN)
            {
                switch(e.key.keysym.sym)
                {
                case SDLK_LEFT:
                    m_scene->setPan(m_scene->getPanX() - 20, m_scene->getPanY());
                    break;
                case SDLK_RIGHT:
                    m_scene->setPan(m_scene->getPanX() + 20, m_scene->getPanY());
                    break;
                case SDLK_UP:
                    m_scene->setPan(m_scene->getPanX(), m_scene->getPanY() - 20);
                    break;
                case SDLK_DOWN:
                    m_scene->setPan(m_scene->getPanX(), m_scene->getPanY() + 20);
                    break;
                case SDLK_EQUALS: // '+' key (zoom in)
                case SDLK_KP_PLUS:
                    m_scene->setZoom(m_scene->getZoom() * 1.1f);
                    break;
                case SDLK_MINUS: // '-' key (zoom out)
                case SDLK_KP_MINUS:
                    m_scene->setZoom(m_scene->getZoom() / 1.1f);
                    break;
                default:
                    break;
                }
            }
            // Zoom with mouse wheel
            if(e.type == SDL_MOUSEWHEEL)
            {
                float zoom = m_scene->getZoom();
                if(e.wheel.y > 0)
                    zoom *= 1.1f;
                else if(e.wheel.y < 0)
                    zoom /= 1.1f;
                m_scene->setZoom(zoom);
            }
        }
    }

    void Engine::update(double dt)
    {
        m_t += dt;
        m_sim.step(dt);
    }

    void Engine::render()
    {
        m_renderer->clear(25, 25, 28, 255);
        m_scene->draw(m_sim.snapshot());
        m_renderer->present();
    }

} // namespace tfv