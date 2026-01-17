#include "ConsoleSink.h"

#include <BECore/Config/XmlNode.h>
#include <BECore/Logger/LogLevel.h>

#include <EASTL/string_view.h>
#include <EASTL/string.h>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>
#include <cstdio>

namespace BECore {

    void ConsoleSink::Initialize() {
        if (_initialized) {
            return;
        }

        LogEvent::Subscribe<&ConsoleSink::OnLogEvent>(this);
        _initialized = true;
    }

    void ConsoleSink::Configure(const XmlNode& node) {
        auto colorEnabled = node.ParseAttribute<bool>("colorEnabled");
        if (colorEnabled.has_value()) {
            SetColorEnabled(*colorEnabled);
        }
    }

    void ConsoleSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void ConsoleSink::Write(LogLevel level, eastl::string_view message) {
        if (!ShouldLog(level)) {
            return;
        }

        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        // Choose output stream
        FILE* stream = (level >= LogLevel::Error) ? stderr : stdout;

        // Format and output
        if (_colorEnabled) {
            const char* color = LogLevelColor(level);
            const char* reset = LogColorReset;

            fmt::print(stream,
                "{}[{:5}]{} [{:%H:%M:%S}.{:03d}] {}\n",
                color, LogLevelToDisplayString(level), reset,
                fmt::localtime(time), ms.count(),
                eastl::string(message.data(), message.size()));
        } else {
            fmt::print(stream,
                "[{:5}] [{:%H:%M:%S}.{:03d}] {}\n",
                LogLevelToDisplayString(level),
                fmt::localtime(time), ms.count(),
                eastl::string(message.data(), message.size()));
        }
    }

    void ConsoleSink::Flush() {
        std::fflush(stdout);
        std::fflush(stderr);
    }

}  // namespace BECore
