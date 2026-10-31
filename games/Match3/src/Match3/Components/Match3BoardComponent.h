#pragma once

#include <BECore/Scene/IComponent.h>
#include <Match3/Game/Match3GameModel.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneDrawEvent;
    }
    namespace SDLEvents {
        struct MouseButtonDownEvent;
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
     * Also draws a small ImGui "Match3" HUD with score and a Reset button.
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

        void OnAttached() override;

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
        void OnDraw(const SceneEvents::SceneDrawEvent&);
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent&);

        Match3GameModel _model;
        static Match3BoardComponent* s_active;
    };

}  // namespace BECore::Match3
