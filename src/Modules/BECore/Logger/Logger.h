#pragma once

#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogLevel.h>
#include <EASTL/string_view.h>

namespace BECore {

    class Logger {
    public:

        // Convenience methods without location info (simple strings)
        static void Debug(eastl::string_view message) {
            LogEvent::Emit(LogLevel::Debug, message);
        }

        static void Info(eastl::string_view message) {
            LogEvent::Emit(LogLevel::Info, message);
        }

        static void Warning(eastl::string_view message) {
            LogEvent::Emit(LogLevel::Warning, message);
        }

        static void Error(eastl::string_view message) {
            LogEvent::Emit(LogLevel::Error, message);
        }

        static void Fatal(eastl::string_view message) {
            LogEvent::Emit(LogLevel::Fatal, message);
            FlushLogsEvent::Emit();
        }
    };

}  // namespace BECore

// Convenience macros (format: [time] [level] message)
#define LOG_DEBUG(...) ::BECore::Logger::Debug(__VA_ARGS__)
#define LOG_INFO(...) ::BECore::Logger::Info(__VA_ARGS__)
#define LOG_WARNING(...) ::BECore::Logger::Warning(__VA_ARGS__)
#define LOG_ERROR(...) ::BECore::Logger::Error(__VA_ARGS__)
#define LOG_FATAL(...) ::BECore::Logger::Fatal(__VA_ARGS__)
