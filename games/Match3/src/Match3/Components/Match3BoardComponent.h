#pragma once

#include <BECore/Scene/IComponent.h>
#include <Match3/Components/BoardAnimation.h>
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
     *
     * A swap that a person makes with the mouse (`ClickCell`) is animated by a `BoardAnimation` on `CoreManager::GetTweens()`: the model is already at
     * the final board while the sprites play, and any change of the model that no click started (a test's `TrySwap`, the control API, a reset) is
     * drawn at once. See "Animation" in `games/Match3/CLAUDE.md`.
     */
    class Match3BoardComponent : public IComponent {
        BE_CLASS(Match3BoardComponent, ICON = grid - 3x3)
    public:
        Match3BoardComponent() = default;
        ~Match3BoardComponent() override;

        BE_REFLECT_FIELD float _originX = 100.0f;
        BE_REFLECT_FIELD float _originY = 80.0f;
        BE_REFLECT_FIELD float _cellSize = 64.0f;
        BE_REFLECT_FIELD uint32_t _seed = 1337;
        BE_REFLECT_FIELD PoolString _menuScene;  ///< the scene that the `Match3Menu` action switches to; empty makes the action do nothing
        BE_REFLECT_FIELD bool _animate = true;   ///< false: a swap by mouse shows its result at once, as the other changes of the model do

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

        /// The click of a person on a cell (what a click on the board does): finishes a running animation, hands the click to the model and, when it
        /// swapped two gems (a match or not), plays the swap. Every click on the board goes through here; a cell outside the board does nothing
        void ClickCell(Cell cell);

        /// True while the board draws the sprites of a swap instead of the model board
        [[nodiscard]] bool IsAnimating() const;

        /// Ends a running animation at once; the next draw shows the model board. How a caller gets the end state without waiting
        void SkipAnimation();

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
        BoardAnimation _animation;
        uint64_t _animatedVersion = 0;   // the model version the running animation belongs to; a draw that finds another one snaps (the change was not animated)
        uint64_t _publishedVersion = 0;  // the model version PublishUiVars() last published; 0 is never a model version
        static Match3BoardComponent* s_active;
    };

}  // namespace BECore::Match3
