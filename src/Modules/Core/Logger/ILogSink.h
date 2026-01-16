#pragma once

#include <Core/Logger/LogLevel.h>

namespace Core {

    /**
     * @brief Interface for log output destinations
     * 
     * Implement this interface to create custom log sinks
     * (e.g., console, file, network, etc.)
     */
    class ILogSink {
    public:
        virtual ~ILogSink() = default;

        /**
         * @brief Write a log message to the sink
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        virtual void Write(LogLevel level, const char* message, const char* file, int line) = 0;

        /**
         * @brief Flush any buffered output
         * 
         * Called to ensure all pending messages are written.
         */
        virtual void Flush() = 0;

        /**
         * @brief Set minimum log level for this sink
         * 
         * Messages below this level will be ignored.
         * 
         * @param level Minimum level to log
         */
        void SetMinLevel(LogLevel level) { _minLevel = level; }

        /**
         * @brief Get minimum log level for this sink
         * 
         * @return Current minimum log level
         */
        LogLevel GetMinLevel() const { return _minLevel; }

        /**
         * @brief Check if a message at given level should be logged
         * 
         * @param level Level to check
         * @return true if the message should be logged
         */
        bool ShouldLog(LogLevel level) const {
            return static_cast<int>(level) >= static_cast<int>(_minLevel);
        }

    protected:
        LogLevel _minLevel = LogLevel::Debug;
    };

}  // namespace Core
