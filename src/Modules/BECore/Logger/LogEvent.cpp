#include "LogEvent.h"

#include <BECore/Logger/LogRingBuffer.h>

namespace BECore {

    void LogEvent::Emit(LogLevel level, StringView message, LogSite site) {
        LogRingBuffer<>::GetGlobal().TryPush(level, message, site);
    }

    void LogEvent::Flush() {
        BE_PROFILE_SCOPE("Log flush");
        LogRingBuffer<>::GetGlobal().Drain([](const LogEntry& entry) { EventBase<LogEvent>::Emit(entry.level, StringView{entry.message, entry.messageLength}, entry.site); });
    }

}  // namespace BECore
