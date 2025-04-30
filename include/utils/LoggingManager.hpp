#pragma once
//
//  Ultra-low-overhead logging (single-producer / single-consumer).
//

#include <algorithm> // copy_n
#include <array>
#include <atomic>
#include <chrono>
#include <cstring> // strrchr
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace utils
{

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define __FILENAME__ ((strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__))
#if defined(__GNUC__) || defined(__clang__)
#define __FUNC_SIG__ __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define __FUNC_SIG__ __FUNCSIG__
#else
#define __FUNC_SIG__ __FUNCTION__
#endif
#if !NDEBUG
#define __LOG_SCOPE__ (std::string(__FILENAME__) + ":" + STR(__LINE__))
#else
#define __LOG_SCOPE__ (std::string(__FILENAME__) + ":" + STR(__LINE__) + " [" + __FUNC_SIG__ + "]")
#endif

    // ───────────────────────────── public API ──────────────────────────────────
    enum class LogLevel : std::uint8_t
    {
        INFO,
        WARNING,
        ERROR,
        DEBUG
    };

    struct LogParam
    {
        enum class Kind : std::uint8_t
        {
            Number,
            String,
            Boolean
        } kind{};
        std::string_view name{};
        std::variant<std::uint64_t, double, std::string, bool> value{};

        constexpr LogParam() noexcept = default; // needed by std::array

        template <typename T,
                  typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
        constexpr LogParam(std::string_view n, T v) noexcept
            : kind(Kind::Number), name(n), value(static_cast<std::uint64_t>(v))
        {
        }

        constexpr LogParam(std::string_view n, double v) noexcept
            : kind(Kind::Number), name(n), value(v)
        {
        }

        constexpr LogParam(std::string_view n, std::string_view v) noexcept
            : kind(Kind::String), name(n), value(std::string(v)) {};
        constexpr LogParam(std::string_view n, const char* v) noexcept
            : kind(Kind::String), name(n), value(std::string(v)) {};
        constexpr LogParam(std::string_view n, bool v) noexcept
            : kind(Kind::Boolean), name(n), value(v)
        {
        }
    };
#define PARAM(name, val) ::utils::LogParam(#name, (val))

    // ─────────────────────────── LoggingManager ───────────────────────────────
    class LoggingManager
    {
      public:
        static LoggingManager& Instance();
        void SetLogFile(const std::string& filename);

        template <LogLevel L, typename... Ps>
        inline void Log(std::string scope, // owns the data!
                        std::string_view fmt, Ps&&... ps) noexcept
        {
            if(L < currentLevel_.load(std::memory_order_relaxed))
                return; // fast path

            static_assert(sizeof...(ps) <= Msg::kMaxParams,
                          "Increase kMaxParams if you need more parameters");

            Msg m;
            m.level = L;
            m.timepoint = std::chrono::system_clock::now();
            m.scope = std::move(scope); // real ownership
            m.fmt = fmt;
            m.paramCount = static_cast<std::uint8_t>(sizeof...(ps));

            std::array<LogParam, sizeof...(ps)> tmp{std::forward<Ps>(ps)...};
            std::copy_n(tmp.begin(), m.paramCount, m.params.begin());

            queue_.enqueue(std::move(m));
        }

        template <typename... Ps> void Info(std::string s, std::string_view f, Ps&&... ps)
        {
            Log<LogLevel::INFO>(std::move(s), f, std::forward<Ps>(ps)...);
        }
        template <typename... Ps> void Warn(std::string s, std::string_view f, Ps&&... ps)
        {
            Log<LogLevel::WARNING>(std::move(s), f, std::forward<Ps>(ps)...);
        }
        template <typename... Ps> void Error(std::string s, std::string_view f, Ps&&... ps)
        {
            Log<LogLevel::ERROR>(std::move(s), f, std::forward<Ps>(ps)...);
        }
        template <typename... Ps> void Debug(std::string s, std::string_view f, Ps&&... ps)
        {
            Log<LogLevel::DEBUG>(std::move(s), f, std::forward<Ps>(ps)...);
        }

      private:
        LoggingManager();
        ~LoggingManager();

        struct Msg
        {
            static constexpr std::size_t kMaxParams = 8;

            LogLevel level{};
            std::chrono::system_clock::time_point timepoint{};
            std::string scope; // now *owns* the data
            std::string_view fmt{};
            std::array<LogParam, kMaxParams> params{};
            std::uint8_t paramCount{};
        };

        class Ring
        {
          public:
            bool enqueue(Msg&& m) noexcept;
            bool dequeue(Msg& m) noexcept;

          private:
            static constexpr std::size_t N = 128;
            std::array<Msg, N> buf_{};
            std::atomic<std::size_t> head_{0}, tail_{0};
        };

        Ring queue_;
        std::thread worker_;
        std::atomic<LogLevel> currentLevel_{LogLevel::INFO};
        std::atomic<bool> running_{true};
        std::ofstream file_;

        void run(); // background thread main loop
    };

// ──────────────────────── user-facing convenience macros ──────────────────
#define LOG_INFO(fmt, ...)                                                                         \
    ::utils::LoggingManager::Instance().Info(__LOG_SCOPE__, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_WARN(fmt, ...)                                                                         \
    ::utils::LoggingManager::Instance().Warn(__LOG_SCOPE__, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                                                        \
    ::utils::LoggingManager::Instance().Error(__LOG_SCOPE__, fmt __VA_OPT__(, ) __VA_ARGS__)
#ifdef NDBUG
#define LOG_DEBUG(fmt, ...)
#else
#define LOG_DEBUG(fmt, ...)                                                                        \
    utils::LoggingManager::Instance().Debug(__LOG_SCOPE__, fmt __VA_OPT__(, ) __VA_ARGS__)
#endif
} // namespace utils