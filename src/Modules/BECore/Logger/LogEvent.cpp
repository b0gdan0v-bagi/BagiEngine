#include "LogEvent.h"

#include <BECore/Logger/LogRingBuffer.h>

namespace BECore {

    void LogEvent::Emit(LogLevel level, StringView message) {
        LogRingBuffer<>::GetGlobal().TryPush(level, message);
    }

    void LogEvent::Flush() {
        BE_PROFILE_SCOPE("Log flush");
        LogRingBuffer<>::GetGlobal().Drain([](const LogEntry& entry) { EventBase<LogEvent>::Emit(entry.level, StringView{entry.message, entry.messageLength}); });
    }

}  // namespace BECore
