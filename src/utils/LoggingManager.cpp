#include "utils/LoggingManager.hpp"

#include <format>
#include <iostream>

namespace utils
{

    // ─────────────────────────── colour tables ────────────────────────────────
    namespace
    {
        constexpr std::array levelStr = {"INFO", "WARNING", "ERROR", "DEBUG"};
        constexpr std::array levelCol = {"\033[1;32m", "\033[1;33m", "\033[1;31m", "\033[0;90m"};

        constexpr std::string_view COL_SCOPE = "\033[1;36m";
        constexpr std::string_view COL_NUM = "\033[1;34m";
        constexpr std::string_view COL_STR = "\033[1;35m";
        constexpr std::string_view COL_BOOL = "\033[1;33m";
        constexpr std::string_view COL_RST = "\033[0m";
    } // namespace

    // ───────────────────────── Ring implementation ────────────────────────────
    bool LoggingManager::Ring::enqueue(Msg&& m) noexcept
    {
        auto h = head_.load(std::memory_order_relaxed);
        auto n = (h + 1) % N;
        if(n == tail_.load(std::memory_order_acquire))
            return false; // full → drop
        buf_[h] = std::move(m);
        head_.store(n, std::memory_order_release);
        return true;
    }

    bool LoggingManager::Ring::dequeue(Msg& m) noexcept
    {
        auto t = tail_.load(std::memory_order_relaxed);
        if(t == head_.load(std::memory_order_acquire))
            return false; // empty
        m = std::move(buf_[t]);
        tail_.store((t + 1) % N, std::memory_order_release);
        return true;
    }

    // ───────────────────── ctor / dtor / basic ops ────────────────────────────
    LoggingManager& LoggingManager::Instance()
    {
        static LoggingManager inst;
        return inst;
    }

    LoggingManager::LoggingManager()
    {
        worker_ = std::thread(&LoggingManager::run, this);
    }

    LoggingManager::~LoggingManager()
    {
        running_.store(false, std::memory_order_relaxed);
        worker_.join();
    }

    void LoggingManager::SetLogFile(const std::string& filename)
    {
        std::ofstream f{filename, std::ios::app};
        file_.swap(f); // worker owns `file_`, so this is safe
    }

    // ─────────────────────── background thread main loop ──────────────────────
    void LoggingManager::run()
    {
        Msg m;

        using SecondsTP = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;
        SecondsTP lastSec{};  // cached “whole-second” part
        std::string secCache; // "YYYY-MM-DD HH:MM:SS"

        while(running_.load(std::memory_order_relaxed) || queue_.dequeue(m))
        {
            if(!queue_.dequeue(m))
            {
                std::this_thread::sleep_for(std::chrono::microseconds{200});
                continue;
            }

            // ───────── timestamp (cached seconds) ─────────────────────────────
            auto tp = std::chrono::time_point_cast<std::chrono::microseconds>(m.timepoint);
            auto sec = std::chrono::time_point_cast<std::chrono::seconds>(tp);
            auto usec = tp - sec; // micro-second fraction

            if(sec != lastSec)
            {                 // refresh once per second
                char buf[20]; // "YYYY-MM-DD HH:MM:SS"
                auto t = std::chrono::system_clock::to_time_t(sec);
                std::tm tm;
                localtime_r(&t, &tm); // use localtime_s on Windows
                strftime(buf, sizeof(buf), "%F %T", &tm);
                secCache.assign(buf, 19); // 19 chars, no trailing '\0'
                lastSec = sec;
            }
            std::string timestamp =
                std::format("{}.{}", secCache, std::format("{:06}", usec.count()));

            // ───────── substitute parameters ────────────────────────────────
            std::string body{m.fmt};
            for(std::size_t i = 0; i < m.paramCount; ++i)
            {
                const auto& p = m.params[i];
                std::string replacement;

                if(p.kind == LogParam::Kind::Number)
                    std::visit([&](auto v)
                               { replacement = std::format("{}{}{}", COL_NUM, v, COL_RST); },
                               p.value);
                else if(p.kind == LogParam::Kind::Boolean)
                    std::visit([&](auto v)
                               { replacement = std::format("{}{}{}", COL_BOOL, v, COL_RST); },
                               p.value);
                else if(p.kind == LogParam::Kind::String)
                    std::visit([&](auto v)
                               { replacement = std::format("{}{}{}", COL_STR, v, COL_RST); },
                               p.value);
                else
                    replacement =
                        std::format("{}{}{}", COL_STR, std::get<std::string>(p.value), COL_RST);

                const std::string placeholder = std::format("{{{}}}", p.name);
                if(auto pos = body.find(placeholder); pos != std::string::npos)
                    body.replace(pos, placeholder.size(), replacement);
            }

            // ───────── final line & output ──────────────────────────────────
            const std::size_t lvl = static_cast<std::size_t>(m.level);
            std::string line =
                std::format("{} [{}{}{}] {}[{}]{} {}\n", timestamp, levelCol[lvl], levelStr[lvl],
                            COL_RST, COL_SCOPE, m.scope, COL_RST, body);

            if(file_.is_open())
                file_.write(line.data(), static_cast<std::streamsize>(line.size()));

            std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
        }
    }

} // namespace utils