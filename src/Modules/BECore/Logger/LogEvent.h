#pragma once

#include <Events/EventBase.h>

namespace BECore {

    struct LogEvent : EventBase<LogEvent> {
        LogLevel level;
        StringView message;

        LogEvent(LogLevel level, StringView message) : level(level), message(message) {}

        static void Emit(LogLevel level, StringView message);

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
