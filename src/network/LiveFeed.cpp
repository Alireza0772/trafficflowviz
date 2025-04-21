#include "network/LiveFeed.hpp"

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

namespace tfv
{
    // DummyFeedHandler implementation
    DummyFeedHandler::DummyFeedHandler(Simulation& sim) : m_sim(sim) {}

    DummyFeedHandler::~DummyFeedHandler()
    {
        stop();
    }

    void DummyFeedHandler::start()
    {
        if(m_running)
            return;
        m_running = true;
        m_thr = std::thread(&DummyFeedHandler::loop, this);
    }

    void DummyFeedHandler::stop()
    {
        m_running = false;
        if(m_thr.joinable())
            m_thr.join();
    }

    bool DummyFeedHandler::isRunning() const
    {
        return m_running;
    }

    void DummyFeedHandler::loop()
    {
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> jitter{-1.f, 1.f};

        while(m_running)
        {
            auto snap = m_sim.snapshot();
            for(auto& [id, v] : snap)
            {
                // Perturb velocity vector instead of speed
                v.vel.x += jitter(rng);
                v.vel.y += jitter(rng);
                m_sim.addVehicle(v); // upsert
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }

    // WebSocketFeedHandler implementation
    WebSocketFeedHandler::WebSocketFeedHandler(Simulation& sim) : m_sim(sim) {}

    WebSocketFeedHandler::~WebSocketFeedHandler()
    {
        stop();
    }

    void WebSocketFeedHandler::start()
    {
        if(m_running)
            return;
        m_running = true;
        m_thr = std::thread(&WebSocketFeedHandler::loop, this);
    }

    void WebSocketFeedHandler::stop()
    {
        m_running = false;
        if(m_thr.joinable())
            m_thr.join();
    }

    bool WebSocketFeedHandler::isRunning() const
    {
        return m_running;
    }

    bool WebSocketFeedHandler::connect()
    {
        // In a real implementation, this would establish a WebSocket connection
        // For now, just log that we're connecting
        std::cout << "[WebSocket] Connecting to " << m_url << "...\n";

        // Simulate connection success for demonstration
        return true;
    }

    void WebSocketFeedHandler::processMessage(const std::string& msg)
    {
        // TODO: In a real implementation, parse JSON/Protobuf message
        // For now, we'll simulate by creating random vehicles

        static uint64_t nextId = 10000; // Start IDs high to avoid conflict with loaded vehicles

        // Simple parsing of dummy format: "vehicle,segmentId,position,velX,velY"
        std::istringstream iss(msg);
        std::string type;
        std::getline(iss, type, ',');

        if(type == "vehicle")
        {
            Vehicle v;
            v.id = nextId++;

            std::string segment;
            std::getline(iss, segment, ',');
            v.segmentId = std::stoul(segment);

            std::string pos;
            std::getline(iss, pos, ',');
            v.position = std::stof(pos);

            std::string velX;
            std::getline(iss, velX, ',');
            v.vel.x = std::stof(velX);

            std::string velY;
            std::getline(iss, velY);
            v.vel.y = std::stof(velY);

            m_sim.addVehicle(v);
        }
    }

    void WebSocketFeedHandler::loop()
    {
        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<uint32_t> segmentDist(1, 10); // Assuming segments 1-10 exist
        std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> velDist(-5.0f, 5.0f);

        while(m_running)
        {
            // Try to connect if not already
            if(!connect())
            {
                std::cout << "[WebSocket] Connection failed, retrying in "
                          << m_reconnectInterval / 1000.0f << " seconds...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(m_reconnectInterval));
                continue;
            }

            // Simulate receiving WebSocket messages
            for(int i = 0; i < 3; ++i)
            { // Simulate receiving 3 messages per batch
                // Create a simulated message for a new vehicle
                std::ostringstream oss;
                oss << "vehicle," << segmentDist(rng) << "," << posDist(rng) << "," << velDist(rng)
                    << "," << velDist(rng);

                processMessage(oss.str());
            }

            // Wait a bit before next batch
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    // LiveFeed implementation
    LiveFeed::LiveFeed(Simulation& sim) : m_sim(sim) {}

    LiveFeed::~LiveFeed()
    {
        disconnect();
    }

    void LiveFeed::connect(const std::string& url, FeedType type)
    {
        disconnect(); // Disconnect any existing feed

        switch(type)
        {
        case FeedType::DUMMY:
            m_handler = std::make_unique<DummyFeedHandler>(m_sim);
            break;
        case FeedType::WEBSOCKET:
            auto wsHandler = std::make_unique<WebSocketFeedHandler>(m_sim);
            wsHandler->setUrl(url);
            m_handler = std::move(wsHandler);
            break;
        }

        m_handler->start();

        if(m_statusCallback)
        {
            m_statusCallback(true, "Connected to feed: " + url);
        }
    }

    void LiveFeed::disconnect()
    {
        if(m_handler)
        {
            m_handler->stop();
            m_handler.reset();

            if(m_statusCallback)
            {
                m_statusCallback(false, "Disconnected from feed");
            }
        }
    }

    bool LiveFeed::isConnected() const
    {
        return m_handler && m_handler->isRunning();
    }

} // namespace tfv