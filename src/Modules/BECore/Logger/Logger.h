#pragma once

#include <BECore/Logger/LogEvent.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class Logger {
    public:
        static void Debug(StringView message, LogSite site = {}) {
            LogEvent::Emit(LogLevel::Debug, message, site);
        }

        static void Info(StringView message, LogSite site = {}) {
            LogEvent::Emit(LogLevel::Info, message, site);
        }

        static void Warning(StringView message, LogSite site = {}) {
            LogEvent::Emit(LogLevel::Warning, message, site);
        }

        static void Error(StringView message, LogSite site = {}) {
            LogEvent::Emit(LogLevel::Error, message, site);
        }

        /**
         * @brief Logs at Fatal level and, on the main thread, delivers the line to the sinks before it returns, so a crash right after loses nothing.
         *
         * Safe from any thread. Off the main thread the line only joins the ring buffer and the sinks get it at the next frame flush.
         */
        static void Fatal(StringView message, LogSite site = {}) {
            LogEvent::Emit(LogLevel::Fatal, message, site);
            FlushOnMainThread();
        }

        /**
         * @brief Delivers the pending lines to the sinks and flushes them, when the caller is the main thread.
         *
         * LogEvent::Flush has a single consumer, the main thread. Until the TaskManager is up no worker exists, so the caller is the main thread.
         * @return true when it flushed, false when the caller is a worker (the frame flush takes the lines).
         */
        static bool FlushOnMainThread();
    };

}  // namespace BECore

#define BE_LOG_SITE ::BECore::MakeLogSite(__FILE__, __LINE__)

#define LOG_DEBUG(...) ::BECore::Logger::Debug(__VA_ARGS__, BE_LOG_SITE)
#define LOG_INFO(...) ::BECore::Logger::Info(__VA_ARGS__, BE_LOG_SITE)
#define LOG_WARNING(...) ::BECore::Logger::Warning(__VA_ARGS__, BE_LOG_SITE)
#define LOG_ERROR(...) ::BECore::Logger::Error(__VA_ARGS__, BE_LOG_SITE)
/// Logs at Fatal level; flushes at once only on the main thread (Logger::Fatal)
#define LOG_FATAL(...) ::BECore::Logger::Fatal(__VA_ARGS__, BE_LOG_SITE)
