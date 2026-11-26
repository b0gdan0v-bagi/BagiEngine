#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>
#include <Games/GameEditorTools.h>
#include <Games/IGame.h>

namespace BECore {

    /**
     * @brief Owns one instance of every registered IGame and tracks the active one.
     *
     * Activation sequence (SwitchTo):
     *   1. Stop() the previously active game.
     *   2. Tell SceneManager to make the new game's scene active.
     *   3. Start() the new game.
     * With the editor its tools (GameEditorTools) are cleared before 1 and registered after 3.
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

        const Vector<IntrusivePtr<IGame>>& GetAll() const {
            return _games;
        }

#if BE_WITH_EDITOR
        /// The editor commands and panels of the active game; empty without one
        [[nodiscard]] GameEditorTools& GetEditorTools() {
            return _editorTools;
        }
#endif

    private:
        GameManager() = default;
        ~GameManager() override = default;

        IntrusivePtr<IGame> FindGame(PoolString name) const;

        Vector<IntrusivePtr<IGame>> _games;
        IntrusivePtr<IGame> _active;
#if BE_WITH_EDITOR
        GameEditorTools _editorTools;
#endif
    };

}  // namespace BECore
