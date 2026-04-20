#pragma once

#include <Games/IGame.h>

namespace BECore {

    /**
     * @brief Owns one instance of every registered IGame and tracks the active one.
     *
     * Initialize() instantiates every game registered in AbstractFactory<IGame>,
     * reads config/GamesConfig.xml to determine the default game, and activates it.
     *
     * Activation sequence (SwitchTo):
     *   1. Stop() the previously active game.
     *   2. Tell SceneManager to make the new game's scene active.
     *   3. Start() the new game.
     */
    class GameManager : public Singleton<GameManager> {
        friend class Singleton<GameManager>;

    public:
        void Initialize();
        void Shutdown();

        /// Activate a game by GetName(). No-op if name is unknown or already active.
        void SwitchTo(PoolString name);

        /// Restart the currently active game (calls IGame::Reset()).
        void ResetActive();

        IGame* GetActive() const {
            return _active.Get();
        }

        const eastl::vector<IntrusivePtr<IGame>>& GetAll() const {
            return _games;
        }

    private:
        GameManager() = default;
        ~GameManager() override = default;

        IntrusivePtr<IGame> FindGame(PoolString name) const;

        eastl::vector<IntrusivePtr<IGame>> _games;
        IntrusivePtr<IGame> _active;
    };

}  // namespace BECore
