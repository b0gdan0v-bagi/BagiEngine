#include "AssetBrowserPanelModel.h"

#include <BECore/File/File.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <EASTL/algorithm.h>
#include <Generated/AssetBrowserPanelModel.gen.hpp>
#include <Widgets/AssetBrowser/AssetFileOps.h>
#include <Widgets/AssetBrowser/AssetReferences.h>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    namespace {

        /// The name of the model's watches in a rescan of the file watcher
        constexpr StringView kWatchConsumer = "AssetBrowser";

        constexpr StringView kSelectFirst = "Select a file first";
        constexpr StringView kRenameHint = "Rename the file. Nothing that names it is rewritten";
        constexpr StringView kDeleteHint = "Delete the file for good";
        constexpr StringView kChangedMeanwhile = "The file or what uses it changed since the dialog showed it: look again, then confirm";

        bool IsCommand(StringView id, PoolString command) {
            return id == command.ToStringView();
        }

        bool IsSameState(const FileInfo& left, const FileInfo& right) {
            return left.size == right.size && left.modifyTimeNs == right.modifyTimeNs;
        }

        bool ShowSameLines(const Vector<AssetUseRow>& left, const Vector<AssetUseRow>& right) {
            return left.size() == right.size() && eastl::equal(left.begin(), left.end(), right.begin(), [](const AssetUseRow& a, const AssetUseRow& b) { return a.text == b.text; });
        }

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

    void AssetBrowserPanelModel::SetDocuments(const EditorDocuments& documents) {
        _documents = &documents;
        _actionsStale = true;
    }

    void AssetBrowserPanelModel::Update() {
        if (_catalogStale) {
            RebuildCatalog();
        }
        if (_listKey.catalogRevision != _catalogRevision || _listKey.folder != _view.folder || _listKey.search != _view.search || _listKey.filter != _view.typeFilter) {
            RebuildShown();
        }
        if (_view.change.renameOpen && _view.change.newName != _checkedName) {
            CheckNewName();
        }
        if (_actionsStale) {
            RefreshActions();
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
            // A question nobody sees must not be confirmed later
            _view.change = AssetChangeView{};
            _pointerOver = false;
        }
    }

    bool AssetBrowserPanelModel::RunCommand(StringView id, StringView /*argument*/) {
        // Any command, a save of another panel too, may change what the buttons depend on; the next Update looks, after every subscriber ran
        _actionsStale = true;
        if (IsCommand(id, EditorCommandIds::kAssetBrowserRescan)) {
            _catalogStale = true;
        } else if (IsCommand(id, EditorCommandIds::kAssetBrowserDelete)) {
            OpenChange(false);
        } else if (IsCommand(id, EditorCommandIds::kAssetBrowserRename)) {
            OpenChange(true);
        } else if (IsCommand(id, EditorCommandIds::kAssetBrowserConfirm)) {
            ConfirmChange();
        } else if (IsCommand(id, EditorCommandIds::kAssetBrowserCancel)) {
            _view.change = AssetChangeView{};
        } else {
            return false;
        }
        Update();
        return true;
    }

    void AssetBrowserPanelModel::SelectFolder(StringView folder) {
        _view.folder.assign(folder.data(), folder.size());
        Update();
    }

    void AssetBrowserPanelModel::SelectAsset(StringView path) {
        Update();
        _view.hasMessage = false;
        _view.message.clear();
        SetSelection(_catalog.Find(path));
        Update();
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
        _actionsStale = true;
    }

    void AssetBrowserPanelModel::RefreshActions() {
        _actionsStale = false;
        String refusal;
        if (!_view.hasSelection) {
            refusal = String(kSelectFirst);
        } else if (const auto target = CheckChange(_view.selectedPath); !target) {
            refusal = target.error();
        }
        _view.canChange = refusal.empty();
        _view.renameHint = _view.canChange ? String(kRenameHint) : refusal;
        _view.deleteHint = _view.canChange ? String(kDeleteHint) : refusal;
    }

    std::expected<AssetFileTarget, String> AssetBrowserPanelModel::CheckChange(StringView path) const {
        const EditorDocuments& documents = _documents != nullptr ? *_documents : CoreManager::GetEditorDocuments();
        // The reference check reads the saved files, and a later Save would write the old path back
        for (const EditorDocumentState& document : documents.Collect()) {
            if (document.dirty) {
                return std::unexpected(Format("Save or revert the unsaved changes first ({})", document.id));
            }
        }
        return CheckAssetChange(Files(), path);
    }

    Vector<AssetUseRow> AssetBrowserPanelModel::ReadUsers(StringView path) const {
        const AssetReferences references = FindAssetReferences(Files(), path);
        Vector<AssetUseRow> rows;
        const auto add = [&rows](String text, PanelTextStyle style) {
            AssetUseRow row;
            row.text = std::move(text);
            row.style = style;
            rows.push_back(std::move(row));
        };
        for (const AssetUse& use : references.uses) {
            add(Format("{}, line {}", use.file, use.line), PanelTextStyle::Normal);
        }
        for (const String& name : references.unchecked) {
            add(Format("{}: could not be checked", name), PanelTextStyle::Warning);
        }
        return rows;
    }

    void AssetBrowserPanelModel::OpenChange(bool rename) {
        if (_view.change.deleteOpen || _view.change.renameOpen || !_view.hasSelection) {
            return;
        }
        _view.hasMessage = false;
        _view.message.clear();
        const auto target = CheckChange(_view.selectedPath);
        const Optional<FileInfo> info = target ? GetFileInfo(target->file.View()) : Optional<FileInfo>();
        if (!target || !info) {
            ShowMessage(target ? Format("'{}' cannot be read", _view.selectedPath) : target.error());
            return;
        }
        _view.change = AssetChangeView{};
        _view.change.path = _view.selectedPath;
        _view.change.lowerLayerShows = target->lowerLayerShows;
        _changeStamp = *info;
        ShowUsers(ReadUsers(_view.change.path));
        if (rename) {
            _view.change.newName = String(AssetRenameStem(_view.change.path));
            _checkedName = _view.change.newName;
            _view.change.renameOpen = true;
        } else {
            _view.change.deleteOpen = true;
        }
    }

    void AssetBrowserPanelModel::ShowUsers(Vector<AssetUseRow> users) {
        _view.change.inUse = !users.empty();
        _view.change.unused = users.empty();
        _view.change.users = std::move(users);
    }

    void AssetBrowserPanelModel::CheckNewName() {
        AssetChangeView& change = _view.change;
        _checkedName = change.newName;
        change.hasProblem = false;
        change.canConfirm = false;
        change.nameRefused = false;
        // The name the file has is where typing starts, not a mistake to point at
        if (StringView(change.newName) == AssetRenameStem(change.path)) {
            return;
        }
        const auto rename = CheckAssetRename(Files(), change.path, change.newName);
        change.canConfirm = rename.has_value();
        change.nameRefused = !rename;
        change.nameRefusal = rename ? String() : rename.error();
    }

    void AssetBrowserPanelModel::ConfirmChange() {
        AssetChangeView& change = _view.change;
        if (!change.deleteOpen && !change.renameOpen) {
            return;
        }
        const bool rename = change.renameOpen;
        const auto target = CheckChange(change.path);
        if (!target) {
            ShowProblem(target.error());
            return;
        }
        if (rename) {
            if (const auto allowed = CheckAssetRename(Files(), change.path, change.newName); !allowed) {
                ShowProblem(allowed.error());
                return;
            }
        }
        // The user said yes to what the dialog showed; anything else is shown first
        const Optional<FileInfo> info = GetFileInfo(target->file.View());
        Vector<AssetUseRow> users = ReadUsers(change.path);
        if (!info || !IsSameState(*info, _changeStamp) || !ShowSameLines(users, change.users) || target->lowerLayerShows != change.lowerLayerShows) {
            _changeStamp = info.value_or(FileInfo{});
            change.lowerLayerShows = target->lowerLayerShows;
            ShowUsers(std::move(users));
            ShowProblem(String(kChangedMeanwhile));
            return;
        }

        const String oldPath = change.path;
        const auto done = rename ? RenameAsset(Files(), oldPath, change.newName) : DeleteAsset(Files(), oldPath);
        _view.change = AssetChangeView{};
        if (!done) {
            ShowMessage(done.error());
            return;
        }
        Evict(oldPath);
        _catalogStale = true;
        Update();
        if (rename) {
            SetSelection(_catalog.Find(done->newPath));
        }
        if (done->lowerLayerShows) {
            ShowMessage(Format("'{}' still leads to a file of a lower layer", oldPath));
        }
    }

    void AssetBrowserPanelModel::ShowProblem(String problem) {
        _view.change.problem = std::move(problem);
        _view.change.hasProblem = true;
    }

    void AssetBrowserPanelModel::ShowMessage(String message) {
        _view.message = std::move(message);
        _view.hasMessage = true;
    }

}  // namespace BECore
