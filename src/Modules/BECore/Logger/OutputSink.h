#pragma once

#include <BECore/Logger/ILogSink.h>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace BECore {

    /**
     * @brief Log sink that outputs to platform-specific debug output
     * 
     * On Windows: Uses OutputDebugString to send logs to Visual Studio Output window
     * On macOS: Uses stderr to send logs to Xcode console (captured by Xcode)
     * On other platforms: No-op (safe to use but won't output anything)
     */
    class OutputSink : public ILogSink {
    public:
        OutputSink() = default;
        ~OutputSink() override = default;

        /**
         * @brief Initialize the Output sink
         */
        void Initialize() override;

        /**
         * @brief Write a log message to platform debug output
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void Write(LogLevel level, const char* message, const char* file, int line) override;

        /**
         * @brief Flush Output (no-op, platform debug output is immediate)
         */
        void Flush() override;

    private:
        bool _initialized = false;
    };

}  // namespace BECore
