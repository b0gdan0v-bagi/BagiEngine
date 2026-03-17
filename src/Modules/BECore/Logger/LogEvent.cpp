#include "LogEvent.h"

#include <BECore/Logger/LogRingBuffer.h>

namespace BECore {

    void LogEvent::Emit(LogLevel level, eastl::string_view message) {
        LogRingBuffer<>::GetGlobal().TryPush(level, message);
    }

    void LogEvent::Flush() {
        LogRingBuffer<>::GetGlobal().Drain([](const LogEntry& entry) { EventBase<LogEvent>::Emit(entry.level, eastl::string_view{entry.message, entry.messageLength}); });
    }

}  // namespace BECore
