#include "LogHistorySink.h"

#include <Generated/LogHistorySink.gen.hpp>

namespace BECore {

    void LogHistorySink::Initialize() {
        if (_initialized) {
            return;
        }

        _history = MakeUnique<LogHistory>(static_cast<size_t>(std::clamp(_capacity, kMinCapacity, kMaxCapacity)));
        Subscribe<LogEvent, &LogHistorySink::OnLogEvent>(this);
        _initialized = true;
    }

    void LogHistorySink::OnLogEvent(const LogEvent& event) {
        Record(event.level, event.message, event.site);
    }

    void LogHistorySink::Write(LogLevel level, StringView message) {
        Record(level, message, LogSite{});
    }

    void LogHistorySink::Record(LogLevel level, StringView message, LogSite site) {
        if (!ShouldLog(level) || !_history) {
            return;
        }

        char text[kLogTimeLength];
        const StringView time = FormatLogTimeNow(text);
        _history->Add(level, message, site, time);
    }

}  // namespace BECore
