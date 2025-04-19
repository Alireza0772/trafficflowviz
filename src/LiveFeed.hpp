#ifndef TFV_LIVE_FEED_HPP
#define TFV_LIVE_FEED_HPP

#include <atomic>
#include <thread>

#include "Simulation.hpp"

namespace tfv
{

    /** Prototype real‑time feed: periodically perturbs vehicle speeds. */
    class LiveFeed
    {
      public:
        explicit LiveFeed(Simulation& sim);
        ~LiveFeed();

        void connect(const std::string& url); // non‑blocking

      private:
        void loop();

        Simulation& m_sim;
        std::thread m_thr;
        std::atomic_bool m_running{false};
    };

} // namespace tfv
#endif