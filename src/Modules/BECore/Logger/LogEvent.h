#pragma once

#include <BECore/Logger/LogLevel.h>
#include <Events/EventBase.h>

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

namespace BECore {

    struct LogEvent : EventBase<LogEvent> {
        LogLevel level;              
        eastl::string_view message;  

        LogEvent(LogLevel level, eastl::string_view message)
            : level(level)
            , message(message)
        {}

        // Buffering support for early logging (before sinks are initialized)
        struct BufferedLogEntry {
            LogLevel level;
            eastl::string message;
        };

        static void EnableBuffering();
        static void DisableBuffering();
        static bool IsBuffering();
        static void Emit(LogLevel level, eastl::string_view message);
        static void FlushBuffer();

    private:
        static bool& GetBufferingFlag();
        static eastl::vector<BufferedLogEntry>& GetBuffer();
    };

    struct FlushLogsEvent : EventBase<FlushLogsEvent> {
    };

}  // namespace BECore
