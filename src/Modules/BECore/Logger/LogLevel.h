#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /** @brief Log severity; sinks filter by this order, from least to most severe. */
    CORE_ENUM(LogLevel, uint8_t,
              Debug,    ///< Detailed debug information (development only)
              Info,     ///< General informational messages
              Warning,  ///< Potentially problematic situations
              Error,    ///< Error conditions that don't stop execution
              Fatal     ///< Critical errors that may cause termination
    )

    /** @brief ANSI escape sequence that colours a console line of this level. */
    constexpr const char* LogLevelColor(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:
                return "\033[36m";  // Cyan
            case LogLevel::Info:
                return "\033[32m";  // Green
            case LogLevel::Warning:
                return "\033[33m";  // Yellow
            case LogLevel::Error:
                return "\033[31m";  // Red
            case LogLevel::Fatal:
                return "\033[35m";  // Magenta
            default:
                return "\033[0m";  // Reset
        }
    }

    constexpr const char* LogColorReset = "\033[0m";

}  // namespace BECore
