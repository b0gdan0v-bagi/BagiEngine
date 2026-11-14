#include "OutputSink.h"

#include <Generated/OutputSink.gen.hpp>
#include <chrono>
#include <cstdio>
#include <fmt/core.h>

namespace BECore {

    void OutputSink::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<LogEvent, &OutputSink::OnLogEvent>(this);
        _initialized = true;
    }

    void OutputSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void OutputSink::Write(LogLevel level, StringView message) {
        if (!ShouldLog(level)) {
            return;
        }

        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Thread-safe localtime conversion
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif

        // Format message similar to console/file sinks (the time is written out, not {:%H:%M:%S}: the Emscripten build cannot use fmt/chrono.h)
        //-NoStd: fmt::format returns std::string, handed to the OS log
        std::string formatted = fmt::format("[{}] [{:02}:{:02}:{:02}.{:03d}] {}\n", level, tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms.count(), message);

#if defined(PLATFORM_WINDOWS)
        // Output to Visual Studio Output window
        OutputDebugStringA(formatted.c_str());
#elif defined(PLATFORM_MACOS)
        // Output to Xcode console (stderr is captured by Xcode)
        std::fprintf(stderr, "%s", formatted.c_str());
        std::fflush(stderr);
#endif
    }

}  // namespace BECore
