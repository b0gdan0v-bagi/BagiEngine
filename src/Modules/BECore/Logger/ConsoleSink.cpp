#include "ConsoleSink.h"

#include <BECore/Logger/LogHistory.h>
#include <Generated/ConsoleSink.gen.hpp>
#include <cstdio>

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
        UnsubscribeAll();
        Subscribe<LogEvent, &ConsoleSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &ConsoleSink::OnFlushEvent>(this);
    }

    void ConsoleSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void ConsoleSink::Write(LogLevel level, StringView message) {
        if (!ShouldLog(level)) {
            return;
        }

        char timeBuffer[kLogTimeLength];
        const StringView time = FormatLogTimeNow(timeBuffer);

        FILE* stream = (level >= LogLevel::Error) ? stderr : stdout;

        if (_colorEnabled && ColorsStream(stream)) {
            const char* color = LogLevelColor(level);
            const char* reset = LogColorReset;

            Print(stream, "{}[{}]{} [{}] {}\n", color, level, reset, time, message);
        } else {
            Print(stream, "[{}] [{}] {}\n", level, time, message);
        }
    }

    void ConsoleSink::OnFlushEvent() {
        std::fflush(stdout);
        std::fflush(stderr);
    }

}  // namespace BECore
