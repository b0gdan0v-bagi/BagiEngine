#include "FileChangesPanelModel.h"

#include <BECore/GameManager/CoreManager.h>
#include <Generated/FileChangesPanelModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    namespace {

        constexpr StringView kConsumerSeparator = ", ";

        void JoinConsumers(const Vector<PoolString>& consumers, String& out) {
            out.clear();
            for (size_t i = 0; i < consumers.size(); ++i) {
                if (i > 0) {
                    out.append(kConsumerSeparator.data(), kConsumerSeparator.size());
                }
                out.append(consumers[i].ToStringView().data(), consumers[i].ToStringView().size());
            }
        }

    }  // namespace

    TypedRef FileChangesPanelModel::GetObject() {
        return TypedRef::Of(_view);
    }

    FileWatcher& FileChangesPanelModel::Watcher() const {
        return _watcher != nullptr ? *_watcher : CoreManager::GetFileWatcher();
    }

    void FileChangesPanelModel::SetWatcher(FileWatcher& watcher) {
        _watcher = &watcher;
        RebuildView();
    }

    void FileChangesPanelModel::Update() {
        if (!_viewBuilt || Watcher().IsRunning() != _view.running) {
            RebuildView();
        }
    }

    bool FileChangesPanelModel::RunCommand(StringView id, StringView argument) {
        const auto is = [id](PoolString command) { return id == command.ToStringView(); };
        if (!is(EditorCommandIds::kFileChangesRescan) && !is(EditorCommandIds::kFileChangesApplyAll) && !is(EditorCommandIds::kFileChangesApply)) {
            return false;
        }
        FileWatcher& watcher = Watcher();
        if (watcher.IsRunning()) {
            if (is(EditorCommandIds::kFileChangesRescan)) {
                _model.Rescan(watcher);
            } else if (is(EditorCommandIds::kFileChangesApplyAll)) {
                _model.ApplyAll(watcher);
            } else {
                // The path is taken from the pending row it names, so a stranger's text is never interned
                for (const PendingFileChange& change : _model.GetPending()) {
                    if (change.virtualPath.ToStringView() == argument) {
                        _model.ApplyOne(watcher, change.virtualPath);
                        break;
                    }
                }
            }
        }
        RebuildView();
        return true;
    }

    void FileChangesPanelModel::RebuildView() {
        const bool running = Watcher().IsRunning();
        const bool hasPending = !_model.GetPending().empty();
        _view.running = running;
        _view.notRunning = !running;
        _view.canApplyAll = running && hasPending;
        _view.showPressRescan = running && !_model.HasScanned();
        _view.showNothingChanged = running && _model.HasScanned() && !hasPending;
        _view.showPending = running && hasPending;
        _view.showApplied = running && !_model.GetApplied().empty();

        _view.pending.resize(_model.GetPending().size());
        for (size_t i = 0; i < _view.pending.size(); ++i) {
            const PendingFileChange& change = _model.GetPending()[i];
            FileChangeRow& row = _view.pending[i];
            row.path = String(change.virtualPath.ToStringView());
            row.kind = String(EnumUtils<FileChangeKind>::ToString(change.kind));
            JoinConsumers(change.consumers, row.consumers);
        }
        _view.applied.resize(_model.GetApplied().size());
        for (size_t i = 0; i < _view.applied.size(); ++i) {
            const AppliedFileChange& change = _model.GetApplied()[i];
            FileApplyRow& row = _view.applied[i];
            row.path = String(change.virtualPath.ToStringView());
            row.error = change.error;
            row.style = change.ok ? PanelTextStyle::Success : PanelTextStyle::Error;
        }
        _viewBuilt = true;
    }

}  // namespace BECore
