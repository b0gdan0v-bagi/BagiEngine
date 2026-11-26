#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>

namespace BECore {

    class IControllableGame;
#if BE_WITH_EDITOR
    class GameEditorTools;
#endif

    /**
     * @brief Base interface for a self-contained game.
     *
     * Each concrete game lives outside the engine, in its own module, derives from IGame
     * and is auto-registered into AbstractFactory<IGame> via BE_CLASS reflection.
     *
     * Lifecycle:
     *   - Start()  — called when the game becomes active. The corresponding scene
     *                (GetSceneName()) is set active in SceneManager just before.
     *   - Stop()   — called before another game becomes active.
     *   - Reset()  — restart a session without changing the active scene.
     */
    class IGame : public RefCounted {
        BE_CLASS(IGame, FACTORY_BASE)
    public:
        IGame() = default;
        ~IGame() override = default;

        /// Unique identifier — also used as the display label and config key.
        virtual PoolString GetName() const = 0;

        /// Name of the Scene (in config/SceneConfig.xml) the game wants active.
        virtual PoolString GetSceneName() const = 0;

        virtual void Start() {}
        virtual void Stop() {}
        virtual void Reset() {}

        /// The interface of the external control API (state, actions, perform), or nullptr when the game cannot be driven
        /// from outside. The engine asks through this call instead of casting, so it never names a concrete game.
        virtual IControllableGame* AsControllable() {
            return nullptr;
        }

#if BE_WITH_EDITOR
        /// Where the game adds its editor commands and debug panels (`.claude/rules/games-editor-tools.md`), called each time it has become active, after Start().
        /// It exists only in a build with the editor, so a game guards its override with `#if BE_WITH_EDITOR` too. A command that changes the state goes through
        /// ControlledReset / ControlledAction / ControlledSetState, or a recording does not replay
        virtual void RegisterEditorTools(GameEditorTools& /*tools*/) {}
#endif
    };

}  // namespace BECore
