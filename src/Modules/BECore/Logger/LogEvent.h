#pragma once

#include <BECore/Logger/LogLevel.h>
#include <Events/EventBase.h>

#include <EASTL/string_view.h>

namespace BECore {

    struct LogEvent : EventBase<LogEvent> {
        LogLevel level;              
        eastl::string_view message;  

        LogEvent(LogLevel level, eastl::string_view message)
            : level(level)
            , message(message)
        {}

        static void Emit(LogLevel level, eastl::string_view message);

        /**
         * @brief Drain the global log ring buffer and dispatch all pending entries to subscribers.
         *
         * Must be called from the main thread (single consumer).
         * Called once per frame from Application::Run() and immediately after Fatal() logs.
         */
        static void Flush();
    };

    struct FlushLogsEvent : EventBase<FlushLogsEvent> {
    };

}  // namespace BECore
