#pragma once

#include <BECore/Logger/LogLevel.h>
#include <BECore/Logger/LogSite.h>
#include <BECore/Types/Types.h>
#include <Events/EventBase.h>

namespace BECore {

    struct LogEvent : EventBase<LogEvent> {
        LogLevel level;
        StringView message;
        LogSite site;

        LogEvent(LogLevel level, StringView message, LogSite site = {}) : level(level), message(message), site(site) {}

        static void Emit(LogLevel level, StringView message, LogSite site = {});

        /**
         * @brief Drain the global log ring buffer and dispatch all pending entries to subscribers.
         *
         * Must be called from the main thread (single consumer).
         * Called once per frame from Application::RunFrame() and immediately after Fatal() logs.
         */
        static void Flush();
    };

    struct FlushLogsEvent : EventBase<FlushLogsEvent> {};

}  // namespace BECore
