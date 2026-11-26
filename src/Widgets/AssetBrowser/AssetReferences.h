#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class FileSystem;

    /// A line of a content file
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

    /// A name in the content that leads to no file
    struct MissingAsset {
        String name;  ///< the path or the node reference as the file writes it, escapes decoded and `\` as `/`
        AssetUse use;
    };

    /// What the saved content as a whole says about its files
    struct ContentAssetCheck {
        Vector<MissingAsset> missing;  ///< by file, line and name
        Vector<String> unused;         ///< virtual paths of files of the content root that no source uses
        /// As in `AssetReferences`: with one, a file of `unused` may have a user and a missing name may be unseen
        Vector<String> unchecked;
    };

    /**
     * @brief Looks for the names that resolve nowhere and the files that nothing uses
     *
     * Reads each source file once per call. The rules of both lists and what they never see: `Widgets/AssetBrowser/CLAUDE.md`, "The content check".
     */
    [[nodiscard]] ContentAssetCheck CheckContentAssets(const FileSystem& fileSystem);

}  // namespace BECore
