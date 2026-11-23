#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogRingBuffer.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// The size of the stack buffer of one OutputSink line: a whole log message and the head `[level] [time] ` fit, a longer line is cut.
    inline constexpr size_t kOutputLineSize = LogEntry::MaxMessageSize + 64;

    /// Writes `[level] [time] message\n` into @p buffer, cut at the buffer and always NUL-terminated (`OutputDebugStringA` needs it);
    /// the view is the text without the NUL and points into the buffer.
    StringView FormatOutputLine(char (&buffer)[kOutputLineSize], LogLevel level, StringView time, StringView message);

    /**
     * @brief Log sink that outputs to platform-specific debug output
     *
     * On Windows: Uses OutputDebugString to send logs to Visual Studio Output window
     * On macOS: Uses stderr to send logs to Xcode console (captured by Xcode)
     * On other platforms: No-op (safe to use but won't output anything)
     */
    class OutputSink : public ILogSink {
        BE_CLASS(OutputSink)

    public:
        OutputSink() = default;
        ~OutputSink() override = default;

        void Initialize() override;
        void Write(LogLevel level, StringView message) override;

    private:
        void OnLogEvent(const LogEvent& event);
    };

}  // namespace BECore
