#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// The names of an exported desktop package that the executable reads. `tools/export/package_desktop.py` writes the same ones
    struct PackageLayout {
        static constexpr StringView kMarkerFile = "bagi-package.txt";  // beside the executable
        static constexpr StringView kContentFolder = "content";
        static constexpr StringView kEngineFolder = "engine";  // below the content folder, as is the game's
        static constexpr StringView kGameFolder = "game";
    };

    /// The content roots of a package, the engine's below the game's
    struct PackagedContentRoots {
        Path engine;
        Path game;
    };

    /// The content roots of the package in @p folder; none when the folder has no marker file, and for an empty @p folder
    [[nodiscard]] Optional<PackagedContentRoots> FindPackagedContentRoots(const Path& folder);

    /// The folder of the running executable as `SDL_GetBasePath` says it; empty when the system cannot say
    [[nodiscard]] Path ExecutableDirectory();

}  // namespace BECore
