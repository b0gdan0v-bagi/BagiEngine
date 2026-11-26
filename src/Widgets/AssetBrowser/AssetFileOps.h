#pragma once

#include <BECore/File/Path.h>
#include <BECore/Resource/AssetKind.h>
#include <BECore/Types/Types.h>
#include <expected>

/**
 * Delete and rename of a content file: the one place that decides whether it may happen and does it (the rules:
 * `Widgets/AssetBrowser/CLAUDE.md`, "Delete and rename"). Every refusal is a sentence for the user and leaves the disk unchanged.
 * Nothing here asks who uses the file (`AssetReferences.h`) or rewrites what names it.
 */

namespace BECore {

    class FileSystem;

    /// A file that may be deleted or renamed
    struct AssetFileTarget {
        Path file;  ///< real
        AssetKind kind = AssetKind::None;
        /// A lower layer holds a file under the same name (a node: or its cooked form), which shows through once this one is gone
        bool lowerLayerShows = false;
    };

    /// A rename that may happen
    struct AssetRename {
        AssetFileTarget source;
        Path target;     ///< real, in the folder of the source
        String newPath;  ///< virtual
    };

    /// What a delete or a rename did
    struct AssetFileChange {
        String newPath;  ///< virtual; empty after a delete
        /// The old path still leads to a file, that of a lower layer
        bool lowerLayerShows = false;
    };

    /// The part of the file's name a rename replaces: all but the extension, which is `.ui.xml` or `.ui.html` for such a file; a view into @p virtualPath
    [[nodiscard]] StringView AssetRenameStem(StringView virtualPath);

    /// Whether a file of this kind may be deleted or renamed
    [[nodiscard]] bool IsChangeableAssetKind(AssetKind kind);

    /**
     * @brief The real file of the asset when it is a file of the content root itself, the only files that are the root's to change
     *
     * Its top layer is the mount folder of `GetRootPath()` and no symbolic link leads there; else the sentence that says why not.
     */
    [[nodiscard]] std::expected<Path, String> ResolveContentRootFile(const FileSystem& fileSystem, StringView path);

    /// Whether the asset of the virtual path @p path may be deleted or renamed at all
    [[nodiscard]] std::expected<AssetFileTarget, String> CheckAssetChange(const FileSystem& fileSystem, StringView path);

    /// Whether the asset may be renamed to @p newStem (`AssetRenameStem`; the folder and the extension stay)
    [[nodiscard]] std::expected<AssetRename, String> CheckAssetRename(const FileSystem& fileSystem, StringView path, StringView newStem);

    /// Removes the file for good, when `CheckAssetChange` allows it
    [[nodiscard]] std::expected<AssetFileChange, String> DeleteAsset(const FileSystem& fileSystem, StringView path);

    /// Renames the file, when `CheckAssetRename` allows it
    [[nodiscard]] std::expected<AssetFileChange, String> RenameAsset(const FileSystem& fileSystem, StringView path, StringView newStem);

}  // namespace BECore
