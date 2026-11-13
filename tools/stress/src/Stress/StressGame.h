#pragma once

#include <Games/IGame.h>
#include <Stress/StressGameControl.h>
#include <Stress/StressSettings.h>
#include <Stress/StressState.h>

namespace BECore::Stress {

    /**
     * @brief The IGame of BagiStress: no rules, only the scene the arguments chose (`--stress-scene=`), shown with `--stress-count=` objects.
     *
     * The settings are parsed from `CoreManager::GetCommandLine()` in the constructor, because GameManager asks GetSceneName() before Start().
     * A bad flag is logged once and the game starts with the defaults. The components read the live scene, count and language from GetState().
     * Owns the adapter of the external control API (StressGameControl), which changes that state; a change of the scene switches the active scene.
     */
    class StressGame : public IGame {
        BE_CLASS(StressGame)
    public:
        StressGame();
        ~StressGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        IControllableGame* AsControllable() override;

        /// The active game when it is a StressGame, else null: a component does nothing in that case (Games/CLAUDE.md, "Gotchas")
        static StressGame* GetActive();

        const StressSettings& GetSettings() const {
            return _settings;
        }

        StressState& GetState() {
            return _state;
        }

        const StressState& GetState() const {
            return _state;
        }

    private:
        /// Makes the scene of the state the active one (after a controller changed the scene)
        void ShowActiveScene();

        StressSettings _settings;
        StressState _state;
        StressGameControl _control;  // after _state: it drives it
    };

}  // namespace BECore::Stress
