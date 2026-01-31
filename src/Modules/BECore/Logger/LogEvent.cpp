#include "LogEvent.h"

#include <mutex>

namespace BECore {

    void LogEvent::Emit(LogLevel level, eastl::string_view message) {
        static std::mutex s_logMutex;
        std::lock_guard lock(s_logMutex);
        EventBase<LogEvent>::Emit(level, message);
    }

}  // namespace BECore
