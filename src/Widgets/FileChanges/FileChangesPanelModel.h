#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Widgets/FileChanges/FileChangesModel.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore {

    /// A row of the list of pending changes, as text
    struct FileChangeRow {
        BE_CLASS(FileChangeRow)

        BE_REFLECT_FIELD String path;
        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD String consumers;
    };

    /// A row of the result of the last apply
    struct FileApplyRow {
        BE_CLASS(FileApplyRow)

        BE_REFLECT_FIELD String path;
        BE_REFLECT_FIELD String error;
        BE_REFLECT_FIELD PanelTextStyle style = PanelTextStyle::Success;
    };

    /**
     * @brief What `FileChangesPanel.xml` binds to: the state of the File changes panel as bools and rows
     *
     * A condition of an asset is one bool (the format has no AND), so every combination the panel shows is a field of its own.
     */
    struct FileChangesView {
        BE_CLASS(FileChangesView)

        BE_REFLECT_FIELD bool running = false;
        BE_REFLECT_FIELD bool canApplyAll = false;
        BE_REFLECT_FIELD bool showPressRescan = false;
        BE_REFLECT_FIELD bool showNothingChanged = false;
        BE_REFLECT_FIELD bool showPending = false;
        BE_REFLECT_FIELD bool showApplied = false;
        BE_REFLECT_FIELD Vector<FileChangeRow> pending;
        BE_REFLECT_FIELD Vector<FileApplyRow> applied;
    };

    /**
     * @brief The model of the File changes panel asset: `FileChangesModel` (no ImGui) behind a reflected view
     *
     * Commands: `fileChanges.rescan`, `fileChanges.applyAll` and `fileChanges.apply` (the argument is the virtual path of a pending row). The disk is read
     * only by a command. The view is rebuilt after a command and when the watcher starts or stops, so a steady frame allocates nothing.
     */
    class FileChangesPanelModel : public IPanelModel {
        BE_CLASS(FileChangesPanelModel)
    public:
        FileChangesPanelModel() = default;

        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        /// The watcher the commands act on; the one of `CoreManager` unless a test sets another. It must outlive the model
        void SetWatcher(FileWatcher& watcher);

        [[nodiscard]] const FileChangesView& GetView() const {
            return _view;
        }

    private:
        [[nodiscard]] FileWatcher& Watcher() const;
        void RebuildView();

        FileChangesModel _model;
        FileChangesView _view;
        FileWatcher* _watcher = nullptr;
        bool _viewBuilt = false;
    };

}  // namespace BECore
