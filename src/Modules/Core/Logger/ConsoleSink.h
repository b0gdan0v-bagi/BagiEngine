#pragma once

#include <Core/Logger/ILogSink.h>

namespace Core {

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
         * @brief Write a log message to console
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void Write(LogLevel level, const char* message, const char* file, int line) override;

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
    };

}  // namespace Core
