#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

#include <fstream>
#include <mutex>

namespace BECore {

    class IDeserializer;

    class FileSink : public ILogSink {
        BE_CLASS(FileSink)

    public:
        FileSink() = default;
        ~FileSink() override;

        void Initialize() override;

        void Configure(IDeserializer& deserializer) override;

        bool IsOpen() const { return _file.is_open(); }

        void Write(LogLevel level, eastl::string_view message) override;

    private:

        void OnLogEvent(const LogEvent& event);

        void OnFlushEvent();

        void SetFilename(eastl::string_view filename);
        const eastl::string& GetFilename() const {
            return _filename;
        }

        void SetAppend(bool append) { _append = append; }
        bool IsAppend() const { return _append; }

    private:
        BE_REFLECT_FIELD eastl::string _filename = "engine.log";
        BE_REFLECT_FIELD bool _append = false;
        std::ofstream _file;
        std::mutex _mutex;
        bool _initialized = false;
    };

}  // namespace BECore
