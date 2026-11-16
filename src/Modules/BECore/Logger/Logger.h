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

        static void Fatal(StringView message, LogSite site = {}) {
            LogEvent::Emit(LogLevel::Fatal, message, site);
            LogEvent::Flush();
            FlushLogsEvent::Emit();
        }
    };

}  // namespace BECore

#define BE_LOG_SITE ::BECore::MakeLogSite(__FILE__, __LINE__)

#define LOG_DEBUG(...) ::BECore::Logger::Debug(__VA_ARGS__, BE_LOG_SITE)
#define LOG_INFO(...) ::BECore::Logger::Info(__VA_ARGS__, BE_LOG_SITE)
#define LOG_WARNING(...) ::BECore::Logger::Warning(__VA_ARGS__, BE_LOG_SITE)
#define LOG_ERROR(...) ::BECore::Logger::Error(__VA_ARGS__, BE_LOG_SITE)
#define LOG_FATAL(...) ::BECore::Logger::Fatal(__VA_ARGS__, BE_LOG_SITE)
