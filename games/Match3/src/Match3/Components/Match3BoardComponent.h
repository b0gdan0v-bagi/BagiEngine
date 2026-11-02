#pragma once

#include <BECore/Scene/IComponent.h>
#include <Match3/Game/Match3GameModel.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
    }

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Match3 {

    /**
     * @brief Renders the Match-3 board background + gems and routes clicks to
     *        the owned Match3GameModel.
     *
     * Geometry (origin + cell size) and the RNG seed are reflected so a level
     * designer can tweak both in the editor. The component owns the model;
     * Match3Game::Start() looks the component up via the active scene and
     * triggers Reset(_seed).
     *
     * The HUD is not drawn here: it is a UI canvas of the scene (`config/SceneConfig.xml`) whose labels follow the variables `score` and `seed`, which this
     * component publishes to `CoreManager::GetUIVars()` whenever the model changed. Its buttons emit actions that this component answers while its scene is active:
     * `Match3Reset` starts the board again with the configured seed, `Match3Menu` switches to the scene `_menuScene`.
     */
    class Match3BoardComponent : public IComponent {
        BE_CLASS(Match3BoardComponent)
    public:
        Match3BoardComponent() = default;
        ~Match3BoardComponent() override;

        BE_REFLECT_FIELD float _originX = 100.0f;
        BE_REFLECT_FIELD float _originY = 80.0f;
        BE_REFLECT_FIELD float _cellSize = 64.0f;
        BE_REFLECT_FIELD uint32_t _seed = 1337;
        BE_REFLECT_FIELD PoolString _menuScene;  ///< the scene that the `Match3Menu` action switches to; empty makes the action do nothing

        void OnAttached() override;
        void OnDraw() override;

        Match3GameModel& GetModel() noexcept {
            return _model;
        }
        const Match3GameModel& GetModel() const noexcept {
            return _model;
        }

        uint32_t GetSeed() const noexcept {
            return _seed;
        }

        /// The currently-attached board, if any. Match-3 assumes a single board
        /// is alive at any time; Match3Game uses this to (re)trigger a model
        /// reset on activation.
        static Match3BoardComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent&);
        void OnAction(const InputEvents::ActionEvent&);

        /// Gives the UI labels the score and the seed; only when the model changed, since a label checks the version of the variables every frame
        void PublishUiVars();

        Match3GameModel _model;
        uint64_t _publishedVersion = 0;  // the model version PublishUiVars() last published; 0 is never a model version
        static Match3BoardComponent* s_active;
    };

}  // namespace BECore::Match3
