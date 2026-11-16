#include "OutputSink.h"

#include <BECore/Logger/LogHistory.h>
#include <Generated/OutputSink.gen.hpp>
#include <cstdio>

#ifdef PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace BECore {

    void OutputSink::Initialize() {
        UnsubscribeAll();
        Subscribe<LogEvent, &OutputSink::OnLogEvent>(this);
    }

    void OutputSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void OutputSink::Write(LogLevel level, StringView message) {
        if (!ShouldLog(level)) {
            return;
        }

        char timeBuffer[kLogTimeLength];
        const String formatted = Format("[{}] [{}] {}\n", level, FormatLogTimeNow(timeBuffer), message);

#if defined(PLATFORM_WINDOWS)
        OutputDebugStringA(formatted.c_str());
#elif defined(PLATFORM_MACOS)
        std::fprintf(stderr, "%s", formatted.c_str());
        std::fflush(stderr);
#endif
    }

}  // namespace BECore
