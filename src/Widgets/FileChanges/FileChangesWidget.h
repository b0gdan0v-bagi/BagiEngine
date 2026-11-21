#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/FileChanges/FileChangesModel.h>

namespace BECore {

    /**
     * @brief The editor's File changes panel: a Rescan button that lists the files that differ on disk from what the engine last knew (path, kind, who
     * would be told), an Apply button for all of them and one per row, and the result of the last apply.
     *
     * Hidden at start; the command `view.fileChanges` shows and hides it. Editor only. The panel reads the disk only when a button is pressed
     * (`FileWatcher::Rescan` stats every watched file), never while it is open and idle, and does nothing while it is closed.
     */
    class FileChangesWidget : public IWidget {
        BE_CLASS(FileChangesWidget)
    public:
        FileChangesWidget() = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void OnCommand(const EditorCommandEvent& event);
        void DrawPending();
        void DrawApplied() const;

        BE_REFLECT_FIELD bool _open = false;
        FileChangesModel _model;
    };

}  // namespace BECore
