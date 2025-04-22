#include "network/LiveFeed.hpp"
#include <chrono>
#include <iostream>
#include <random>

namespace tfv
{
    // Dummy feed handler implementation
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
        if(!m_running)
            return;

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
        std::random_device rd;
        std::mt19937 gen(rd());

        // Generate random vehicle updates every few seconds
        while(m_running)
        {
            // Sleep for a bit
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // In a real implementation, this would add/update vehicles
            // But for a dummy feed, we'll do nothing
        }
    }

    // WebSocket feed handler stub implementation
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
        if(!m_running)
            return;

        m_running = false;
        if(m_thr.joinable())
            m_thr.join();
    }

    bool WebSocketFeedHandler::isRunning() const
    {
        return m_running;
    }

    void WebSocketFeedHandler::loop()
    {
        while(m_running)
        {
            if(connect())
            {
                // In a real implementation, this would run the WebSocket protocol
                // For now, just sleep
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            else
            {
                // Sleep then retry connection
                std::this_thread::sleep_for(std::chrono::milliseconds(m_reconnectInterval));
            }
        }
    }

    bool WebSocketFeedHandler::connect()
    {
        // In a real implementation, this would establish a WebSocket connection
        // For now, pretend it worked
        return true;
    }

    void WebSocketFeedHandler::processMessage(const std::string& msg)
    {
        // In a real implementation, this would parse JSON and update vehicles
    }

    // LiveFeed implementation
    LiveFeed::LiveFeed(Simulation& sim) : m_sim(sim) {}

    LiveFeed::~LiveFeed()
    {
        disconnect();
    }

    void LiveFeed::connect(const std::string& url, FeedType type)
    {
        // Disconnect if already connected
        disconnect();

        // Create handler based on feed type
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

        if(m_handler)
        {
            m_handler->start();

            if(m_statusCallback)
            {
                m_statusCallback(true, "Connected to feed");
            }
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
