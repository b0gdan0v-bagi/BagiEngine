#pragma once

namespace BECore {

    /// Stops in the debugger. On Linux and the web it is `__builtin_trap()`; on the web there is no debugger.
    inline void DebugBreak() {
#if defined(PLATFORM_WINDOWS)
        __debugbreak();
#elif defined(PLATFORM_MACOS)
        __builtin_debugtrap();
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_WEB)
        __builtin_trap();
#else
#include <csignal>
        raise(SIGTRAP);
#endif
    }

}  // namespace BECore

#if defined(PLATFORM_WINDOWS)
#define ENGINE_DEBUG_BREAK() __debugbreak()
#elif defined(PLATFORM_MACOS)
#define ENGINE_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_WEB)
#define ENGINE_DEBUG_BREAK() __builtin_trap()
#else
#define ENGINE_DEBUG_BREAK() ((void)0)
#endif
