#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

namespace BECore {

    class XmlNode;

    /**
     * @brief Log sink that outputs to console (stdout/stderr)
     * 
     * Supports colored output using ANSI escape codes.
     * Error and Fatal messages go to stderr, others to stdout.
     */
    class ConsoleSink : public ILogSink {
    public:
        ConsoleSink() = default;
        ~ConsoleSink() override = default;

        /**
         * @brief Initialize the console sink
         */
        void Initialize() override;

        /**
         * @brief Configure the console sink from XML node
         * 
         * @param node XML node containing sink configuration
         */
        void Configure(const XmlNode& node) override;

        /**
         * @brief Write a log message to console
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
         * @brief Flush console output
         */
        void Flush() override;

        /**
         * @brief Enable or disable colored output
         * 
         * @param enabled true to enable colors
         */
        void SetColorEnabled(bool enabled) { _colorEnabled = enabled; }

        /**
         * @brief Check if colored output is enabled
         * 
         * @return true if colors are enabled
         */
        bool IsColorEnabled() const { return _colorEnabled; }

    private:
        bool _colorEnabled = true;
        bool _initialized = false;
    };

}  // namespace BECore
