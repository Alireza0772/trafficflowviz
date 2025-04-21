#ifndef TFV_LIVE_FEED_HPP
#define TFV_LIVE_FEED_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/Simulation.hpp"

namespace tfv
{
    enum class FeedType
    {
        DUMMY,    // Dummy feed for testing
        WEBSOCKET // WebSocket feed
    };

    class IFeedHandler
    {
      public:
        virtual ~IFeedHandler() = default;
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual bool isRunning() const = 0;
    };

    class DummyFeedHandler : public IFeedHandler
    {
      public:
        explicit DummyFeedHandler(Simulation& sim);
        ~DummyFeedHandler() override;

        void start() override;
        void stop() override;
        bool isRunning() const override;

      private:
        void loop();

        Simulation& m_sim;
        std::thread m_thr;
        std::atomic_bool m_running{false};
    };

    class WebSocketFeedHandler : public IFeedHandler
    {
      public:
        explicit WebSocketFeedHandler(Simulation& sim);
        ~WebSocketFeedHandler() override;

        void setUrl(const std::string& url) { m_url = url; }
        void setReconnectInterval(int ms) { m_reconnectInterval = ms; }

        void start() override;
        void stop() override;
        bool isRunning() const override;

      private:
        void loop();
        bool connect();
        void processMessage(const std::string& msg);

        Simulation& m_sim;
        std::string m_url;
        std::thread m_thr;
        std::atomic_bool m_running{false};
        int m_reconnectInterval{5000}; // ms
    };

    /** Real-time data feed manager */
    class LiveFeed
    {
      public:
        explicit LiveFeed(Simulation& sim);
        ~LiveFeed();

        void connect(const std::string& url, FeedType type = FeedType::WEBSOCKET);
        void disconnect();
        bool isConnected() const;

        // Set callback for connection status changes
        using StatusCallback = std::function<void(bool connected, const std::string& message)>;
        void setStatusCallback(StatusCallback cb) { m_statusCallback = cb; }

      private:
        Simulation& m_sim;
        std::unique_ptr<IFeedHandler> m_handler;
        StatusCallback m_statusCallback;
    };

} // namespace tfv
#endif