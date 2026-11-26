#include "MainToolbarModel.h"

#include <Generated/MainToolbarModel.gen.hpp>
#include <Widgets/EditorLayout/IEditorLayoutHost.h>

namespace BECore {

    TypedRef MainToolbarModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void MainToolbarModel::Update() {
        const IEditorLayoutHost* host = EditorLayoutHostRegistry::GetInstance().Get();
        _view = MainToolbarView{};
        if (host == nullptr) {
            return;
        }
        const Optional<EditorWorkspace> workspace = host->GetActiveWorkspace();
        _view.hasWorkspace = workspace.has_value();
        _view.workspace = workspace.value_or(EditorWorkspace::Scene);
        const EditorPlayModel* play = host->GetPlayModel();
        if (play == nullptr) {
            return;
        }
        const EditorPlayState state = play->GetState();
        _view.stopped = state == EditorPlayState::Stopped;
        _view.playing = state == EditorPlayState::Playing;
        _view.paused = state == EditorPlayState::Paused;
        _view.canPause = play->CanPause();
        _view.canStep = play->CanStep();
    }

    bool MainToolbarModel::RunCommand(StringView /*id*/, StringView /*argument*/) {
        return false;
    }

}  // namespace BECore
