#include "LogHistorySink.h"

#include <Generated/LogHistorySink.gen.hpp>
#include <chrono>

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

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif

        // Written out like the other sinks do, not {:%H:%M:%S}: the Emscripten build cannot use fmt/chrono.h
        char text[kLogTimeLength];
        const FormatResult written = FormatToN(text, sizeof(text), "{:02}:{:02}:{:02}.{:03d}", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms.count());
        _history->Add(level, message, site, StringView{text, std::min(written.size, sizeof(text))});
    }

}  // namespace BECore
