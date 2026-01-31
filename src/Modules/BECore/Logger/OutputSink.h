#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace BECore {

    class IDeserializer;

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
        void Configure(IDeserializer& /*deserializer*/) override {}
        void Write(LogLevel level, eastl::string_view message) override;

    private:

        void OnLogEvent(const LogEvent& event);

    private:
        bool _initialized = false;
    };

}  // namespace BECore
