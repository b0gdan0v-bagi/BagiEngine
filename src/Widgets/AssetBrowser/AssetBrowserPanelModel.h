#pragma once

#include <BECore/FileSystem/FileWatcher.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Resource/AssetKind.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <Widgets/AssetBrowser/AssetBrowserSlots.h>
#include <Widgets/AssetBrowser/AssetCatalog.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief What the Asset Browser's asset binds to
     *
     * `search` and `typeFilter` are written by the controls of the panel and read by the model in its next `Update`; the rest is written by the model.
     * A condition of an asset is one bool (the format has no AND), so every state the panel shows is a field of its own.
     */
    struct AssetBrowserView {
        BE_CLASS(AssetBrowserView)

        BE_REFLECT_FIELD String search;
        BE_REFLECT_FIELD AssetTypeFilter typeFilter = AssetTypeFilter::All;
        /// The selected folder of the tree; empty is the root above the mounts
        BE_REFLECT_FIELD String folder = "assets";
        BE_REFLECT_FIELD bool hasSelection = false;
        /// The virtual path of the selected asset, empty with none
        BE_REFLECT_FIELD String selectedPath;
        BE_REFLECT_FIELD AssetKind selectedKind = AssetKind::None;
        /// The folder, the search and the filter leave files to show, or none
        BE_REFLECT_FIELD bool showFiles = false;
        BE_REFLECT_FIELD bool showEmpty = false;
    };

    /**
     * @brief The model of the Asset Browser: the catalog, the shown files and the selection, kept up with the disk, with no ImGui
     *
     * The catalog is made at the first `Update` and again only after a watched file changed or on `assetBrowser.rescan` (`RunCommand`, which an
     * asset routes only while its panel is open); the shown list only when the catalog, the folder, the search or the filter moved, so a steady
     * `Update` allocates nothing. `Initialize` watches the asset mounts: a notice marks the catalog stale for the next `Update` and drops a changed
     * texture from the resource cache at once, so a closed panel keeps the cache right too. It also registers the slots that draw the tree and the
     * tiles (`AssetBrowserSlots`); what they note is applied in `EndUpdate`. Refresh rules: `Widgets/AssetBrowser/CLAUDE.md`.
     */
    class AssetBrowserPanelModel : public IPanelModel {
        BE_CLASS(AssetBrowserPanelModel)
    public:
        /// Drops the texture of a virtual path from whatever caches it
        using Eviction = MoveOnlyFunction<void(StringView)>;

        AssetBrowserPanelModel() = default;

        /// Starts the watches and registers the slots; called once by the widget that hosts the model, after the seams below are set
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        void EndUpdate() override;
        void OnOpenChanged(bool open) override;
        bool RunCommand(StringView id, StringView argument) override;

        /// Lists @p fileSystem instead of CoreManager's (a test); it must outlive the model
        void SetFileSystem(const FileSystem& fileSystem);
        /// Watches through @p watcher instead of CoreManager's; before `Initialize`, and it must outlive the model
        void SetWatcher(FileWatcher& watcher);
        /// Evicts through @p eviction instead of CoreManager's resource cache
        void SetEviction(Eviction eviction);

        /// Selects a folder of the tree; one the catalog does not have becomes its nearest parent that it has
        void SelectFolder(StringView folder);
        /// Selects a listed file; any other text selects nothing
        void SelectAsset(StringView path);

        /// A click of this frame on a folder of the tree, applied in `EndUpdate`
        void NoteFolderClick(PoolString folder) {
            _clickedFolder = folder;
        }

        /// A click of this frame on a tile, applied in `EndUpdate`
        void NoteAssetClick(PoolString path) {
            _clickedAsset = path;
        }

        [[nodiscard]] const AssetCatalog& GetCatalog() const {
            return _catalog;
        }

        /// Indices into `GetCatalog().GetFiles()` of the files to show; valid until the model is updated or told to select
        [[nodiscard]] Span<const uint32_t> GetShownFiles() const {
            return {_shown.data(), _shown.size()};
        }

        /// Moves each time the catalog is made again: a holder of something read from the files (a failed thumbnail) tries again then
        [[nodiscard]] uint32_t GetCatalogRevision() const {
            return _catalogRevision;
        }

        [[nodiscard]] const AssetBrowserView& GetView() const {
            return _view;
        }

    private:
        /// What the shown list was last made from
        struct ListKey {
            uint32_t catalogRevision = 0;  ///< 0: never made
            String folder;
            String search;
            AssetTypeFilter filter = AssetTypeFilter::All;
        };

        [[nodiscard]] const FileSystem& Files() const;
        [[nodiscard]] FileWatcher& Watcher() const;
        void OnFileChanged(StringView virtualPath);
        void Evict(StringView virtualPath);
        void RebuildCatalog();
        void RebuildShown();
        void SetSelection(const AssetFile* file);

        AssetCatalog _catalog;
        AssetBrowserView _view;
        Vector<uint32_t> _shown;
        ListKey _listKey;
        const FileSystem* _fileSystem = nullptr;
        FileWatcher* _watcher = nullptr;
        Eviction _eviction;
        Vector<FileWatchHandle> _watches;
        AssetBrowserSlots _slots{*this};
        Optional<PoolString> _clickedFolder;  ///< the empty string is the root, so "no click" is the empty optional
        Optional<PoolString> _clickedAsset;
        uint32_t _catalogRevision = 0;
        bool _catalogStale = true;
    };

}  // namespace BECore
