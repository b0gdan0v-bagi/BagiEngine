#pragma once

#include <BECore/File/TextFileWriter.h>
#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// The line buffer of a FileSink: a whole log message and the head `[level] [date time.ms] ` of its line fit, so the line is never cut.
    inline constexpr size_t kFileSinkBufferSize = LogEntry::MaxMessageSize + 64;

    class FileSink : public ILogSink {
        BE_CLASS(FileSink)

    public:
        FileSink() = default;
        /// The writer's destructor writes the buffered lines and closes the file
        ~FileSink() override = default;

        void Initialize() override;

        bool IsOpen() const {
            return _writer.IsOpen();
        }

        void Write(LogLevel level, StringView message) override;

    private:
        void OnLogEvent(const LogEvent& event);

        void OnFlushEvent();

        void SetFilename(StringView filename);
        const String& GetFilename() const {
            return _filename;
        }

        void SetAppend(bool append) {
            _append = append;
        }
        bool IsAppend() const {
            return _append;
        }

    private:
        BE_REFLECT_FIELD String _filename = "engine.log";
        BE_REFLECT_FIELD bool _append = false;
        TextFileWriter<kFileSinkBufferSize> _writer;
        Mutex _mutex{BE_LOCK_SITE("Log file sink")};
        bool _initialized = false;
    };

}  // namespace BECore
