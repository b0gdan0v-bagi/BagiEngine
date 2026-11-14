#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

namespace BECore {

    class FileSink : public ILogSink {
        BE_CLASS(FileSink)

    public:
        FileSink() = default;
        ~FileSink() override;

        void Initialize() override;

        bool IsOpen() const {
            return _file.is_open();
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
        std::ofstream _file;
        Mutex _mutex{BE_LOCK_SITE("Log file sink")};
        bool _initialized = false;
    };

}  // namespace BECore
