#pragma once

/**
 * @file PlatformDebug.h
 * @brief Platform-specific debug utilities
 * 
 * Provides cross-platform debug break functionality.
 */

namespace BECore {

    /**
     * @brief Triggers a debugger breakpoint
     * 
     * On Windows: Uses __debugbreak() intrinsic
     * On macOS/Linux: Uses __builtin_debugtrap() or raise(SIGTRAP)
     */
    inline void DebugBreak() {
#if defined(PLATFORM_WINDOWS)
        __debugbreak();
#elif defined(PLATFORM_MACOS)
        __builtin_debugtrap();
#elif defined(PLATFORM_LINUX)
        __builtin_trap();
#else
        // Fallback: use SIGTRAP signal
        #include <csignal>
        raise(SIGTRAP);
#endif
    }

}  // namespace BECore

// Macro for use in assert expressions
#if defined(PLATFORM_WINDOWS)
    #define ENGINE_DEBUG_BREAK() __debugbreak()
#elif defined(PLATFORM_MACOS)
    #define ENGINE_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(PLATFORM_LINUX)
    #define ENGINE_DEBUG_BREAK() __builtin_trap()
#else
    #define ENGINE_DEBUG_BREAK() ((void)0)
#endif
