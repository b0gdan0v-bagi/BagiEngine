#include "ConsoleSink.h"

#include <BECore/Reflection/IArchive.h>
#include <BECore/Logger/LogLevel.h>

#include <EASTL/string_view.h>
#include <EASTL/string.h>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>
#include <cstdio>

#include <Generated/ConsoleSink.gen.hpp>

namespace BECore {

    void ConsoleSink::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<LogEvent, &ConsoleSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &ConsoleSink::OnFlushEvent>(this);
        _initialized = true;
    }

    void ConsoleSink::Configure(IArchive& archive) {
        archive.SerializeAttribute("colorEnabled", _colorEnabled);
    }

    void ConsoleSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void ConsoleSink::Write(LogLevel level, eastl::string_view message) {
        if (!ShouldLog(level)) {
            return;
        }

        // Get current time
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        // Choose output stream
        FILE* stream = (level >= LogLevel::Error) ? stderr : stdout;

        // Format and output
        if (_colorEnabled) {
            const char* color = LogLevelColor(level);
            const char* reset = LogColorReset;

            fmt::print(stream,
                "{}[{}]{} [{:%H:%M:%S}.{:03d}] {}\n",
                color, level, reset,
                fmt::localtime(time), ms.count(), message);
        } else {
            fmt::print(stream,
                "[{}] [{:%H:%M:%S}.{:03d}] {}\n",
                level, fmt::localtime(time), ms.count(), message);
        }
    }

    void ConsoleSink::OnFlushEvent() {
        std::fflush(stdout);
        std::fflush(stderr);
    }

}  // namespace BECore
