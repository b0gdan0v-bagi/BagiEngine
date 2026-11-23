#include "FileSink.h"

#include <BECore/Logger/LogTime.h>
#include <Generated/FileSink.gen.hpp>

namespace BECore {

    void FileSink::Initialize() {
        UnsubscribeAll();
        Subscribe<LogEvent, &FileSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &FileSink::OnFlushEvent>(this);

        if (_initialized) {
            return;
        }

        _initialized = true;

        if (const auto opened = _writer.Open(_filename, _append ? FileMode::Append : FileMode::Write); !opened) {
            LOG_WARNING("FileSink: cannot open the log file: {}", opened.error());
        }
    }

    void FileSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void FileSink::OnFlushEvent() {
        LockGuard lock(_mutex);
        _writer.Flush();
    }

    void FileSink::Write(LogLevel level, StringView message) {
        if (!ShouldLog(level) || !_writer.IsOpen()) {
            return;
        }

        const LocalLogTime now = GetLocalLogTime();

        LockGuard lock(_mutex);

        // The time is written out, not {:%Y-%m-%d %H:%M:%S}: the Emscripten build cannot use fmt/chrono.h
        _writer.AppendFormat("[{}] [{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03d}] {}\n", level, now.tm.tm_year + 1900, now.tm.tm_mon + 1, now.tm.tm_mday, now.tm.tm_hour, now.tm.tm_min, now.tm.tm_sec,
                             now.millisecond, message);
    }

    void FileSink::SetFilename(StringView filename) {
        _filename = filename;
    }

}  // namespace BECore
