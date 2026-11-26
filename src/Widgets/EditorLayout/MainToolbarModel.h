#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <Widgets/PanelAssets/IPanelModel.h>

namespace BECore {

    /// What `MainToolbarPanel.xml` binds to: the state of the editor's play model as flags the buttons read
    struct MainToolbarView {
        BE_CLASS(MainToolbarView)

        BE_REFLECT_FIELD bool stopped = true;
        BE_REFLECT_FIELD bool playing = false;
        BE_REFLECT_FIELD bool paused = false;
        BE_REFLECT_FIELD bool canPause = false;
        BE_REFLECT_FIELD bool canStep = false;
    };

    /**
     * @brief The model of the main toolbar (an asset of the window kind `Bar`): a copy of the host's `EditorPlayModel` in `Update`
     *
     * The buttons run `game.play`, `game.pause` and `game.step`, which `EditorLayoutWidget` owns, so the model runs no command. With no host the view is a stopped
     * game and every button that needs the game is disabled.
     */
    class MainToolbarModel : public IPanelModel {
        BE_CLASS(MainToolbarModel)
    public:
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const MainToolbarView& GetView() const {
            return _view;
        }

    private:
        MainToolbarView _view;
    };

}  // namespace BECore
