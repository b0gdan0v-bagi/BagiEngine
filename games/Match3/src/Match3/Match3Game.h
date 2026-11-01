#pragma once

#include <Games/IGame.h>
#include <Match3/Match3GameControl.h>

namespace BECore::Match3 {

    /**
     * @brief Concrete IGame for the Bejeweled-style Match-3 MVP.
     *
     * Stateless game wrapper: the actual board, gem grid and score live inside
     * the active Match3BoardComponent (which the scene owns). Start() looks the
     * component up via the active scene and triggers a Reset(seed); Reset()
     * does the same on demand.
     *
     * Auto-registered into AbstractFactory<IGame> via the BE_CLASS reflection
     * machinery — no extra hand-written registrar required.
     *
     * Owns the adapter of the external control API (Match3GameControl), which reaches the
     * model of the active board component.
     */
    class Match3Game : public IGame {
        BE_CLASS(Match3Game)
    public:
        Match3Game();
        ~Match3Game() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        void Start() override;
        void Stop() override;
        void Reset() override;

        IControllableGame* AsControllable() override;

    private:
        Match3GameControl _control;
    };

}  // namespace BECore::Match3
