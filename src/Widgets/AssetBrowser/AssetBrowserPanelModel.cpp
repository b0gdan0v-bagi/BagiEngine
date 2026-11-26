#include "AssetBrowserPanelModel.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Generated/AssetBrowserPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    namespace {

        /// The name of the model's watches in a rescan of the file watcher
        constexpr StringView kWatchConsumer = "AssetBrowser";

        bool IsFolderOnDisk(const Path& path) {
            const Optional<FileInfo> info = GetFileInfo(path.View());
            return info && info->kind == FileKind::Directory;
        }

    }  // namespace

    void AssetBrowserPanelModel::Initialize() {
        for (const PoolString mount : GetAssetMounts()) {
            _watches.push_back(Watcher().WatchFolder(mount.ToStringView(), [this](FileChange& change) { OnFileChanged(change.virtualPath.ToStringView()); }, kWatchConsumer));
        }
        _slots.Register();
    }

    TypedRef AssetBrowserPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    const FileSystem& AssetBrowserPanelModel::Files() const {
        return _fileSystem != nullptr ? *_fileSystem : CoreManager::GetFileSystem();
    }

    FileWatcher& AssetBrowserPanelModel::Watcher() const {
        return _watcher != nullptr ? *_watcher : CoreManager::GetFileWatcher();
    }

    void AssetBrowserPanelModel::SetFileSystem(const FileSystem& fileSystem) {
        _fileSystem = &fileSystem;
        _catalogStale = true;
    }

    void AssetBrowserPanelModel::SetWatcher(FileWatcher& watcher) {
        _watcher = &watcher;
    }

    void AssetBrowserPanelModel::SetEviction(Eviction eviction) {
        _eviction = std::move(eviction);
    }

    void AssetBrowserPanelModel::Update() {
        if (_catalogStale) {
            RebuildCatalog();
        }
        if (_listKey.catalogRevision != _catalogRevision || _listKey.folder != _view.folder || _listKey.search != _view.search || _listKey.filter != _view.typeFilter) {
            RebuildShown();
        }
    }

    void AssetBrowserPanelModel::EndUpdate() {
        if (_clickedFolder) {
            SelectFolder(_clickedFolder->ToStringView());
            _clickedFolder.reset();
        }
        if (_clickedAsset) {
            SelectAsset(_clickedAsset->ToStringView());
            _clickedAsset.reset();
        }
    }

    void AssetBrowserPanelModel::OnOpenChanged(bool open) {
        if (!open) {
            _slots.ReleaseThumbnails();
        }
    }

    bool AssetBrowserPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        if (id != EditorCommandIds::kAssetBrowserRescan.ToPoolString().ToStringView()) {
            return false;
        }
        _catalogStale = true;
        Update();
        return true;
    }

    void AssetBrowserPanelModel::SelectFolder(StringView folder) {
        _view.folder.assign(folder.data(), folder.size());
        Update();
    }

    void AssetBrowserPanelModel::SelectAsset(StringView path) {
        Update();
        SetSelection(_catalog.Find(path));
    }

    void AssetBrowserPanelModel::OnFileChanged(StringView virtualPath) {
        const AssetKind kind = AssetKindOfName(virtualPath);
        if (kind != AssetKind::None) {
            _catalogStale = true;
            if (kind == AssetKind::Texture) {
                Evict(virtualPath);
            }
            return;
        }
        // A folder that was renamed or moved may be reported as itself and not file by file
        if (!_catalog.HasFolder(virtualPath) && !IsFolderOnDisk(Files().ResolvePath(virtualPath))) {
            return;
        }
        _catalogStale = true;
        for (const AssetFile& file : _catalog.GetFiles()) {
            if (file.kind == AssetKind::Texture && PathIsUnder(file.path.ToStringView(), virtualPath)) {
                Evict(file.path.ToStringView());
            }
        }
    }

    void AssetBrowserPanelModel::Evict(StringView virtualPath) {
        if (_eviction) {
            _eviction(virtualPath);
        } else {
            CoreManager::GetResourceManager().Evict(virtualPath);
        }
    }

    void AssetBrowserPanelModel::RebuildCatalog() {
        _catalog.Rebuild(Files());
        _catalogStale = false;
        ++_catalogRevision;
        if (_view.hasSelection) {
            SetSelection(_catalog.Find(_view.selectedPath));
        }
    }

    void AssetBrowserPanelModel::RebuildShown() {
        while (!_view.folder.empty() && !_catalog.HasFolder(_view.folder)) {
            _view.folder = String(PathParent(_view.folder));
        }
        _catalog.CollectShown(_view.folder, _view.search, _view.typeFilter, _shown);
        _view.showFiles = !_shown.empty();
        _view.showEmpty = _shown.empty();

        _listKey.catalogRevision = _catalogRevision;
        _listKey.folder = _view.folder;
        _listKey.search = _view.search;
        _listKey.filter = _view.typeFilter;
    }

    void AssetBrowserPanelModel::SetSelection(const AssetFile* file) {
        _view.hasSelection = file != nullptr;
        _view.selectedKind = file != nullptr ? file->kind : AssetKind::None;
        const StringView path = file != nullptr ? file->path.ToStringView() : StringView{};
        _view.selectedPath.assign(path.data(), path.size());
    }

}  // namespace BECore
