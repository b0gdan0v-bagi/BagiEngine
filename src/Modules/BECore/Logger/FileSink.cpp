#include "FileSink.h"

#include <Generated/FileSink.gen.hpp>
#include <chrono>
#include <fmt/core.h>

namespace BECore {

    FileSink::~FileSink() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    void FileSink::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<LogEvent, &FileSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &FileSink::OnFlushEvent>(this);

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

        // Get current time
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Thread-safe localtime conversion
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif

        LockGuard lock(_mutex);

        // Format and output (no colors in file; the time is written out, not {:%Y-%m-%d %H:%M:%S}: the Emscripten build cannot use fmt/chrono.h)
        _file << fmt::format("[{}] [{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03d}] {}\n", level, tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                             ms.count(), message.data());
    }

    void FileSink::SetFilename(StringView filename) {
        _filename = filename;
    }

}  // namespace BECore
