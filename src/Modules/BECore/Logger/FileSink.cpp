#include "FileSink.h"

#include <BECore/Logger/LogTime.h>
#include <Generated/FileSink.gen.hpp>

namespace BECore {

    FileSink::~FileSink() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    void FileSink::Initialize() {
        UnsubscribeAll();
        Subscribe<LogEvent, &FileSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &FileSink::OnFlushEvent>(this);

        if (_initialized) {
            return;
        }

        auto mode = std::ios::out;
        if (_append) {
            mode |= std::ios::app;
        }
        _file.open(_filename.c_str(), mode);

        _initialized = true;
    }

    void FileSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void FileSink::OnFlushEvent() {
        LockGuard lock(_mutex);
        if (_file.is_open()) {
            _file.flush();
        }
    }

    void FileSink::Write(LogLevel level, StringView message) {
        if (!ShouldLog(level) || !_file.is_open()) {
            return;
        }

        const LocalLogTime now = GetLocalLogTime();

        LockGuard lock(_mutex);

        // The time is written out, not {:%Y-%m-%d %H:%M:%S}: the Emscripten build cannot use fmt/chrono.h
        const String line = Format("[{}] [{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03d}] {}\n", level, now.tm.tm_year + 1900, now.tm.tm_mon + 1, now.tm.tm_mday, now.tm.tm_hour, now.tm.tm_min,
                                   now.tm.tm_sec, now.millisecond, message);
        _file << line.c_str();
    }

    void FileSink::SetFilename(StringView filename) {
        _filename = filename;
    }

}  // namespace BECore
