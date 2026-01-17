#pragma once

#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    /**
     * @brief Log severity levels
     * 
     * Used to categorize log messages by importance.
     * Levels are ordered from least to most severe.
     */
    CORE_ENUM(LogLevel, uint8_t,
        Debug,    ///< Detailed debug information (development only)
        Info,     ///< General informational messages
        Warning,  ///< Potentially problematic situations
        Error,    ///< Error conditions that don't stop execution
        Fatal     ///< Critical errors that may cause termination
    )

    /**
     * @brief Get ANSI color code for log level (for console output)
     * 
     * @param level The log level
     * @return ANSI escape sequence for the color
     */
    constexpr const char* LogLevelColor(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:   return "\033[36m";  // Cyan
            case LogLevel::Info:    return "\033[32m";  // Green
            case LogLevel::Warning: return "\033[33m";  // Yellow
            case LogLevel::Error:   return "\033[31m";  // Red
            case LogLevel::Fatal:   return "\033[35m";  // Magenta
            default:                return "\033[0m";   // Reset
        }
    }

    /**
     * @brief ANSI reset color code
     */
    constexpr const char* LogColorReset = "\033[0m";

}  // namespace BECore
