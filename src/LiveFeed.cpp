#include "LiveFeed.hpp"

#include <chrono>
#include <iostream>
#include <random>

namespace tfv
{

    LiveFeed::LiveFeed(Simulation& sim) : m_sim(sim) {}

    LiveFeed::~LiveFeed()
    {
        m_running = false;
        if(m_thr.joinable())
            m_thr.join();
    }

    void LiveFeed::connect(const std::string& url)
    {
        std::cout << "(stub) connecting to " << url << " …\n";
        m_running = true;
        m_thr = std::thread(&LiveFeed::loop, this);
    }

    void LiveFeed::loop()
    {
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> jitter{-1.f, 1.f};

        while(m_running)
        {
            auto snap = m_sim.snapshot();
            for(auto& [id, v] : snap)
            {
                v.speed += jitter(rng);
                m_sim.addVehicle(v); // upsert
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }

} // namespace tfv