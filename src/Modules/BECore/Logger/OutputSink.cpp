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

    StringView FormatOutputLine(char (&buffer)[kOutputLineSize], LogLevel level, StringView time, StringView message) {
        // The last byte is kept for the terminator
        const FormatResult written = FormatToN(buffer, kOutputLineSize - 1, "[{}] [{}] {}\n", level, time, message);
        const size_t length = std::min(written.size, kOutputLineSize - 1);
        buffer[length] = '\0';
        return StringView{buffer, length};
    }

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
        char lineBuffer[kOutputLineSize];
        const StringView line = FormatOutputLine(lineBuffer, level, FormatLogTimeNow(timeBuffer), message);

#if defined(PLATFORM_WINDOWS)
        OutputDebugStringA(line.data());
#elif defined(PLATFORM_MACOS)
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fflush(stderr);
#endif
    }

}  // namespace BECore
