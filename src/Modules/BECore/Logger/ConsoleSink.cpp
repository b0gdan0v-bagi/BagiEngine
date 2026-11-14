#include "ConsoleSink.h"

#include <Generated/ConsoleSink.gen.hpp>
#include <chrono>
#include <cstdio>
#include <fmt/core.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace BECore {

    namespace {

        bool IsTerminal(FILE* stream) {
#ifdef _WIN32
            return _isatty(_fileno(stream)) != 0;
#else
            return isatty(fileno(stream)) != 0;
#endif
        }

        // The colour codes are for a person at a terminal; a pipe or a file (a container's log, a test's capture) gets plain text
        bool ColorsStream(FILE* stream) {
            static const bool stdoutIsTerminal = IsTerminal(stdout);
            static const bool stderrIsTerminal = IsTerminal(stderr);
            return stream == stderr ? stderrIsTerminal : stdoutIsTerminal;
        }

    }  // namespace

    void ConsoleSink::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<LogEvent, &ConsoleSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &ConsoleSink::OnFlushEvent>(this);
        _initialized = true;
    }

    void ConsoleSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void ConsoleSink::Write(LogLevel level, StringView message) {
        if (!ShouldLog(level)) {
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

        // Choose output stream
        FILE* stream = (level >= LogLevel::Error) ? stderr : stdout;

        // Format and output (the time is written out, not {:%H:%M:%S}: the Emscripten build cannot use fmt/chrono.h)
        if (_colorEnabled && ColorsStream(stream)) {
            const char* color = LogLevelColor(level);
            const char* reset = LogColorReset;

            fmt::print(stream, "{}[{}]{} [{:02}:{:02}:{:02}.{:03d}] {}\n", color, level, reset, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms.count(), message);
        } else {
            fmt::print(stream, "[{}] [{:02}:{:02}:{:02}.{:03d}] {}\n", level, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms.count(), message);
        }
    }

    void ConsoleSink::OnFlushEvent() {
        std::fflush(stdout);
        std::fflush(stderr);
    }

}  // namespace BECore
