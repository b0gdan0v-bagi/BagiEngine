#pragma once

#include <BECore/PoolString/StaticPoolString.h>

namespace BECore {

    /// The mount points the engine itself mounts from every content root (`FileSystem::Initialize`).
    struct MountPoints {
        static constexpr auto assets = "assets"_intern;
        static constexpr auto config = "config"_intern;
    };

}  // namespace BECore
