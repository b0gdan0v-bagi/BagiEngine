#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class FileSystem;

    /// One line of a content file that names an asset
    struct AssetUse {
        String file;        ///< virtual, such as `config/TextConfig.xml`; `artgen/...` is below the content root
        uint32_t line = 0;  ///< the first line is 1
    };

    /// What the saved content says about an asset
    struct AssetReferences {
        Vector<AssetUse> uses;
        /// Files and folders (ending in `/`) the check could not read: they may name the asset, so with one the asset is not known to be unused
        Vector<String> unchecked;
    };

    /**
     * @brief Looks through the saved content files for the asset of the virtual path @p assetPath
     *
     * Reads every source file on each call. What counts as a use and what is never seen: `Widgets/AssetBrowser/CLAUDE.md`, "Who uses a file".
     */
    [[nodiscard]] AssetReferences FindAssetReferences(const FileSystem& fileSystem, StringView assetPath);

}  // namespace BECore
