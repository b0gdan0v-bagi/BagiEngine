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
    };

    struct FlushLogsEvent : EventBase<FlushLogsEvent> {
    };

}  // namespace BECore
