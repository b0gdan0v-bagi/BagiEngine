#include "ConsoleSink.h"

#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>
#include <cstdio>

namespace Core {

    void ConsoleSink::Write(LogLevel level, const char* message, const char* file, int line) {
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

            if (file && line > 0) {
                fmt::print(stream, 
                    "{}[{:5}]{} [{:%H:%M:%S}.{:03d}] {} ({}:{})\n",
                    color, LogLevelToString(level), reset,
                    fmt::localtime(time), ms.count(),
                    message, file, line);
            } else {
                fmt::print(stream,
                    "{}[{:5}]{} [{:%H:%M:%S}.{:03d}] {}\n",
                    color, LogLevelToString(level), reset,
                    fmt::localtime(time), ms.count(),
                    message);
            }
        } else {
            if (file && line > 0) {
                fmt::print(stream,
                    "[{:5}] [{:%H:%M:%S}.{:03d}] {} ({}:{})\n",
                    LogLevelToString(level),
                    fmt::localtime(time), ms.count(),
                    message, file, line);
            } else {
                fmt::print(stream,
                    "[{:5}] [{:%H:%M:%S}.{:03d}] {}\n",
                    LogLevelToString(level),
                    fmt::localtime(time), ms.count(),
                    message);
            }
        }
    }

    void ConsoleSink::Flush() {
        std::fflush(stdout);
        std::fflush(stderr);
    }

}  // namespace Core
