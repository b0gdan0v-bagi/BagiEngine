#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Match3 {

    /**
     * @brief The main menu of Match3: answers the actions of the buttons of the menu screen while the menu scene is active.
     *
     * `Match3Play` starts a new game on the board (the configured seed) and switches to `_gameScene`; `Match3Quit` ends the application.
     * The screen itself is a UI canvas next to this component in `config/SceneConfig.xml`; this class only knows what its buttons mean.
     */
    class Match3MenuComponent : public IComponent {
        BE_CLASS(Match3MenuComponent)
    public:
        Match3MenuComponent() = default;
        ~Match3MenuComponent() override = default;

        BE_REFLECT_FIELD PoolString _gameScene;  ///< the scene `Match3Play` switches to

        void OnAttached() override;

    private:
        void OnAction(const InputEvents::ActionEvent&);
    };

}  // namespace BECore::Match3
