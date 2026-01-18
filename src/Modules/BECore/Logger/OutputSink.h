#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace BECore {

    class XmlNode;

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

        /**
         * @brief Initialize the Output sink
         */
        void Initialize() override;

        /**
         * @brief Configure the Output sink from XML node
         * 
         * OutputSink has no configuration options, so this is a no-op.
         * 
         * @param node XML node containing sink configuration
         */
        void Configure(const XmlNode& node) override {}

        /**
         * @brief Write a log message to platform debug output
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         */
        void Write(LogLevel level, eastl::string_view message) override;

    private:
        /**
         * @brief Handle LogEvent
         */
        void OnLogEvent(const LogEvent& event);

        /**
         * @brief Flush Output (no-op, platform debug output is immediate)
         */
        void Flush() override;

    private:
        bool _initialized = false;
    };

}  // namespace BECore
