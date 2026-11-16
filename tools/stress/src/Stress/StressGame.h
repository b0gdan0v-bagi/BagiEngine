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
     *
     * The reflected members are the surface the control API's by-name methods (`get_property`, `set_property`, `call_method`) reach under the
     * root `game`: the proof that they work on a real game, with nothing the scenes show depending on them.
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

        /// Sets the count of objects as StressState::SetCount does: a count above kMaxCount becomes kMaxCount (the `set_count` action refuses it instead)
        BE_FUNCTION void SetObjectCount(uint32_t count);

        /// The scene, the count and the language of the state as one line (`scene=sprites count=100 language=en`), for a controller to compare with `get_state`
        BE_FUNCTION String Describe() const;

        /// Copies the version of the state into the read-only `stateVersion`; called every frame by the run component, so it follows the control API's actions
        void SyncStateVersion();

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

        /// A number a controller sets and reads back; no scene shows it
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1000), tooltip("A number to set and read back; nothing shows it")) uint32_t _runNumber = 0;
        /// StressState::Version() as of the last SyncStateVersion(); never written by a client
        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly, tooltip("The version of the shown state")) uint64_t _stateVersion = 0;

        StressSettings _settings;
        StressState _state;
        StressGameControl _control;  // after _state: it drives it
    };

}  // namespace BECore::Stress
