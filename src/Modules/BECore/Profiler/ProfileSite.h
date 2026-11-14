#pragma once

namespace BECore {

    /**
     * @brief The static place of a zone in the code, made once by a BE_PROFILE_* macro (`static constexpr`).
     *
     * Its address is the identity of the zone: backends keep the pointer, never a copy, and the per-zone statistics are keyed by it.
     * The layout (name, function, file, line, color) is the one a source location of the Tracy backend has, so that backend passes the
     * pointer through; the Tracy backend checks it with static_asserts. Keep the fields and their order.
     */
    struct ProfileSite {
        const char* name;
        const char* function;
        const char* file;
        uint32_t line;
        uint32_t color;  // 0x00RRGGBB, 0 is the backend's default
    };

    /// The allocations a zone made, inclusive of the zones inside it (all zero without ENGINE_PROFILER_ALLOC)
    struct ZoneAllocs {
        uint64_t count = 0;
        uint64_t bytes = 0;
    };

}  // namespace BECore
