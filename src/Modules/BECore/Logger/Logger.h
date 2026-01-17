#pragma once

#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogLevel.h>
#include <EASTL/string_view.h>

namespace BECore {

    class Logger {
    public:

        template <typename... Args>
        static void Log(LogLevel level, fmt::format_string<Args...> format, Args&&... args) {
            eastl::string message = fmt::format(format, std::forward<Args>(args)...);
            LogEvent::Emit(level, message);
        }

        // Convenience methods without location info (simple strings)
        static void Debug(eastl::string_view message) {
            LogRaw(LogLevel::Debug, message);
        }

        static void Info(eastl::string_view message) {
            LogRaw(LogLevel::Info, message);
        }

        static void Warning(eastl::string_view message) {
            LogRaw(LogLevel::Warning, message);
        }

        static void Error(eastl::string_view message) {
            LogRaw(LogLevel::Error, message);
        }

        static void Fatal(eastl::string_view message) {
            LogRaw(LogLevel::Fatal, message);
            FlushLogsEvent::Emit();
        }

        template <typename... Args>
        static void Debug(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Debug, format, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Info(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Info, format, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Warning(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Warning, format, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Error(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Error, format, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Fatal(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Fatal, format, std::forward<Args>(args)...);
            FlushLogsEvent::Emit();
        }

    private:
        static void LogRaw(LogLevel level, eastl::string_view message) {
            LogEvent::Emit(level, message);
        }
    };

}  // namespace BECore

// Convenience macros (format: [time] [level] message)
#define LOG_DEBUG(...) ::BECore::Logger::Debug(__VA_ARGS__)
#define LOG_INFO(...) ::BECore::Logger::Info(__VA_ARGS__)
#define LOG_WARNING(...) ::BECore::Logger::Warning(__VA_ARGS__)
#define LOG_ERROR(...) ::BECore::Logger::Error(__VA_ARGS__)
#define LOG_FATAL(...) ::BECore::Logger::Fatal(__VA_ARGS__)
