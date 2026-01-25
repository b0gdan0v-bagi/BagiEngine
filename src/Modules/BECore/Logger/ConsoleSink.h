#pragma once

#include <BECore/Logger/ILogSink.h>

namespace BECore {

    class IArchive;
    struct LogEvent;

    class ConsoleSink : public ILogSink {
        BE_CLASS(ConsoleSink)

    public:
        ConsoleSink() = default;
        ~ConsoleSink() override = default;

        void Initialize() override;

        void Configure(IArchive& archive) override;

        void Write(LogLevel level, eastl::string_view message) override;

    private:

        void OnLogEvent(const LogEvent& event);
        void OnFlushEvent();

        void SetColorEnabled(bool enabled) { _colorEnabled = enabled; }
        bool IsColorEnabled() const { return _colorEnabled; }

    private:
        BE_REFLECT_FIELD bool _colorEnabled = true;
        bool _initialized = false;
    };

}  // namespace BECore
