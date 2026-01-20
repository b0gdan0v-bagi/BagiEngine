#include "OutputSink.h"

#include <BECore/Logger/LogLevel.h>

#include <EASTL/string_view.h>
#include <EASTL/string.h>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>
#include <string>
#include <cstdio>

#include <Generated/OutputSink.gen.hpp>

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

    void OutputSink::Write(LogLevel level, eastl::string_view message) {
        if (!ShouldLog(level)) {
            return;
        }

        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        // Format message similar to console/file sinks
        std::string formatted = fmt::format("[{}] [{:%H:%M:%S}.{:03d}] {}\n", level, fmt::localtime(time), ms.count(), message);

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
