#pragma once

namespace BECore {

    class IControllableGame;

    /**
     * @brief Base interface for a self-contained game.
     *
     * Each concrete game lives outside the engine, in its own module, derives from IGame
     * and is auto-registered into AbstractFactory<IGame> via BE_CLASS reflection.
     *
     * GameManager enumerates AbstractFactory<IGame>::GetRegisteredTypes() at startup,
     * instantiates one of each, and activates the default (configured in
     * config/GamesConfig.xml) or the first registered if none is specified.
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
    };

}  // namespace BECore
