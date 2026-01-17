#include "OutputSink.h"

#include <BECore/Logger/LogLevel.h>

#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>
#include <string>
#include <cstdio>

namespace BECore {

    void OutputSink::Initialize() {
        if (_initialized) {
            return;
        }

        _initialized = true;
    }

    void OutputSink::Write(LogLevel level, const char* message, const char* file, int line) {
        if (!ShouldLog(level)) {
            return;
        }

        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        // Format message similar to console/file sinks
        std::string formatted;
        if (file && line > 0) {
            formatted = fmt::format(
                "[{:5}] [{:%H:%M:%S}.{:03d}] {} ({}:{})\n",
                LogLevelToDisplayString(level),
                fmt::localtime(time), ms.count(),
                message, file, line);
        } else {
            formatted = fmt::format(
                "[{:5}] [{:%H:%M:%S}.{:03d}] {}\n",
                LogLevelToDisplayString(level),
                fmt::localtime(time), ms.count(),
                message);
        }

#if defined(PLATFORM_WINDOWS)
        // Output to Visual Studio Output window
        OutputDebugStringA(formatted.c_str());
#elif defined(PLATFORM_MACOS)
        // Output to Xcode console (stderr is captured by Xcode)
        std::fprintf(stderr, "%s", formatted.c_str());
        std::fflush(stderr);
#endif
    }

    void OutputSink::Flush() {
        // Platform debug output is immediate, no flushing needed
    }

}  // namespace BECore
