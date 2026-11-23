#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Types/Types.h>
#include <algorithm>
#include <utility>

namespace BECore {

    /**
     * @brief The log entry points behind the LOG_* macros.
     *
     * Each level has two forms, both taking the site first. `Info(site, text)` logs the text as it is: no format parsing, so a `{`
     * in data is safe. `Info(site, "text {}", args...)` checks the format string against the argument types at compile time and
     * formats into a stack buffer of one ring cell, so it allocates on no thread; a line longer than the cell is cut, as a plain
     * text is. The form with a trailing `LogSite` (`Info(text, site)`) is the plain text only.
     */
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

        static void Debug(LogSite site, StringView message) {
            Debug(message, site);
        }

        static void Info(LogSite site, StringView message) {
            Info(message, site);
        }

        static void Warning(LogSite site, StringView message) {
            Warning(message, site);
        }

        static void Error(LogSite site, StringView message) {
            Error(message, site);
        }

        /// Like the one-string Fatal: flushes at once on the main thread
        static void Fatal(LogSite site, StringView message) {
            Fatal(message, site);
        }

        template <typename... Args>
        requires(sizeof...(Args) > 0) static void Debug(LogSite site, FormatString<Args...> fmt, Args&&... args) {
            LogFormatted(LogLevel::Debug, site, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        requires(sizeof...(Args) > 0) static void Info(LogSite site, FormatString<Args...> fmt, Args&&... args) {
            LogFormatted(LogLevel::Info, site, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        requires(sizeof...(Args) > 0) static void Warning(LogSite site, FormatString<Args...> fmt, Args&&... args) {
            LogFormatted(LogLevel::Warning, site, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        requires(sizeof...(Args) > 0) static void Error(LogSite site, FormatString<Args...> fmt, Args&&... args) {
            LogFormatted(LogLevel::Error, site, fmt, std::forward<Args>(args)...);
        }

        /// Like the one-string Fatal: flushes at once on the main thread
        template <typename... Args>
        requires(sizeof...(Args) > 0) static void Fatal(LogSite site, FormatString<Args...> fmt, Args&&... args) {
            LogFormatted(LogLevel::Fatal, site, fmt, std::forward<Args>(args)...);
            FlushOnMainThread();
        }

        /**
         * @brief Delivers the pending lines to the sinks and flushes them, when the caller is the main thread.
         *
         * LogEvent::Flush has a single consumer, the main thread. Until the TaskManager is up no worker exists, so the caller is the main thread.
         * @return true when it flushed, false when the caller is a worker (the frame flush takes the lines).
         */
        static bool FlushOnMainThread();

    private:
        /// Formats outside the ring's claim: a Formatter of a user type may be slow or log itself, and must never hold up the single consumer
        template <typename... Args>
        static void LogFormatted(LogLevel level, LogSite site, FormatString<Args...> fmt, Args&&... args) {
            constexpr size_t kCapacity = LogEntry::MaxMessageSize - 1;
            char buffer[kCapacity];
            const FormatResult result = FormatToN(buffer, kCapacity, fmt, std::forward<Args>(args)...);
            LogEvent::Emit(level, StringView{buffer, std::min(result.size, kCapacity)}, site);
        }
    };

}  // namespace BECore

#define BE_LOG_SITE ::BECore::MakeLogSite(__FILE__, __LINE__)

/// One argument is logged as it is; with more, the first is a format string checked at compile time (Logger)
#define LOG_DEBUG(...) ::BECore::Logger::Debug(BE_LOG_SITE, __VA_ARGS__)
#define LOG_INFO(...) ::BECore::Logger::Info(BE_LOG_SITE, __VA_ARGS__)
#define LOG_WARNING(...) ::BECore::Logger::Warning(BE_LOG_SITE, __VA_ARGS__)
#define LOG_ERROR(...) ::BECore::Logger::Error(BE_LOG_SITE, __VA_ARGS__)
/// Logs at Fatal level; flushes at once only on the main thread (Logger::Fatal)
#define LOG_FATAL(...) ::BECore::Logger::Fatal(BE_LOG_SITE, __VA_ARGS__)
