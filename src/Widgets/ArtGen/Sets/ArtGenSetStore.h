#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <expected>
#include <filesystem>

namespace BECore {

    /// Where the sets of a game live, under its root: `artgen/out/<set>/`
    inline constexpr StringView kArtGenOutFolder = "artgen/out";
    inline constexpr StringView kArtGenManifestFile = "manifest.json";
    /// Why a job that was Pending or Running when the editor died comes back Cancelled
    inline constexpr StringView kEditorClosedReason = "the editor was closed";

    [[nodiscard]] std::filesystem::path SetFolder(const std::filesystem::path& gameRoot, StringView set);
    [[nodiscard]] std::filesystem::path ManifestPath(const std::filesystem::path& gameRoot, StringView set);
    [[nodiscard]] std::filesystem::path JobFolder(const std::filesystem::path& gameRoot, StringView set, StringView job);

    /// The `outDir` of a request on the wire: `<set>/<job>`, under `artgen/out/`
    [[nodiscard]] String OutDirOf(StringView set, StringView job);

    /// True when @p name is a valid set name and no folder of that name exists yet, so a new set never lands in the files of another
    [[nodiscard]] bool NewSetFolderFree(const std::filesystem::path& gameRoot, StringView name);

    /// Writes `<set folder>/manifest.json` so a reader sees the old file or the new one, never half of it; makes the folders.
    /// Called at every change of a job
    [[nodiscard]] std::expected<void, String> WriteManifest(const std::filesystem::path& gameRoot, const ArtGenSet& set);

    /**
     * @brief Every `artgen/out/<set>/manifest.json` of the game, oldest first (`created`, then name).
     *
     * The folder's name is the set's name (the folder may have been renamed). A job that was Pending or Running comes back Cancelled with
     * kEditorClosedReason. A manifest that does not read, or a folder whose name is not a valid set name, is skipped with one warning
     * and never touched. A file a variant names that is gone stays in the set: the review shows a gap, nothing is rewritten at load.
     */
    [[nodiscard]] Vector<ArtGenSet> LoadSets(const std::filesystem::path& gameRoot);

    /// The manifest's form of the real path of a file the service reported: relative to @p setFolder with `/` (also from a `\` path);
    /// a path outside the folder is kept as given, with `/`
    [[nodiscard]] String RelativeToSet(const std::filesystem::path& setFolder, StringView realPath);

    /// The real path of a file of the manifest: relative to the set's folder, or as it is when the manifest holds an absolute path (a file the
    /// service wrote outside the folder). Empty when the file is empty or climbs out of the folder with `..`
    [[nodiscard]] std::filesystem::path AbsoluteInSet(const std::filesystem::path& gameRoot, StringView set, StringView file);

}  // namespace BECore
