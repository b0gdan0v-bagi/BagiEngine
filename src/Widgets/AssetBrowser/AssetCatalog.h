#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/AssetKind.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class FileSystem;

    /// What the browser's type filter lets through: every kind, or one. The values after `All` are those of `AssetKind`
    CORE_ENUM(AssetTypeFilter, uint8_t, All, Texture, Font, Sound, Scene, Node, UiDocument, Config, Other)

    /// A listed content file
    struct AssetFile {
        PoolString path;  ///< virtual, such as `assets/fonts/lucide.ttf`
        AssetKind kind = AssetKind::None;
    };

    /// A folder that holds a listed file at some depth
    struct AssetFolder {
        PoolString path;     ///< virtual, such as `assets/fonts`
        uint32_t depth = 0;  ///< 0 for a mount point
    };

    /// The mount points the browser shows
    [[nodiscard]] Span<const PoolString> GetAssetMounts();

    /**
     * @brief The kind the name of a file says, or `None` for a file the browser does not list
     *
     * A `.xml` under `config` reads as `Config` here: whether it is a scene or a node file is decided by `AssetCatalog::Rebuild`.
     */
    [[nodiscard]] AssetKind AssetKindOfName(StringView virtualPath);

    /// Whether the file is the Generate panel's record beside a picture or a sound, `<name>.artgen.json`: part of that file, not an asset of its own
    [[nodiscard]] bool IsAssetSidecar(StringView virtualPath);

    /**
     * @brief The content files of the asset mounts, their kinds and their folder tree, as of the last `Rebuild`
     *
     * The rules of what is listed and shown: `Widgets/AssetBrowser/CLAUDE.md`.
     */
    class AssetCatalog {
    public:
        /// Lists every layer of the asset mounts again; reads the `.xml` files under `config` to tell their kinds
        void Rebuild(const FileSystem& fileSystem);

        /// The mounts in the order of `GetAssetMounts`, each in the order of `FileSystem::EnumerateFiles`
        [[nodiscard]] Span<const AssetFile> GetFiles() const {
            return {_files.data(), _files.size()};
        }

        /// A parent before its children, siblings in path order
        [[nodiscard]] Span<const AssetFolder> GetFolders() const {
            return {_folders.data(), _folders.size()};
        }

        /// The file of @p path, or null; valid until the next `Rebuild`
        [[nodiscard]] const AssetFile* Find(StringView path) const;

        [[nodiscard]] bool HasFolder(StringView path) const;

        /**
         * @brief Fills @p out with the indices into `GetFiles()` of the files to show, in catalog order
         *
         * With no @p search and the filter `All` they are the files directly in @p folder. Otherwise they are the files at any depth below it whose
         * name holds @p search (ASCII case ignored) and whose kind passes @p filter. An empty @p folder is the root above the mounts.
         */
        void CollectShown(StringView folder, StringView search, AssetTypeFilter filter, Vector<uint32_t>& out) const;

    private:
        Vector<AssetFile> _files;
        Vector<AssetFolder> _folders;
    };

}  // namespace BECore
