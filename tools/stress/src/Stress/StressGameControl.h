#pragma once

#include <Games/IControllableGame.h>
#include <Stress/StressState.h>

namespace BECore::Stress {

    /**
     * @brief The external control API of BagiStress: what the app shows, and a way to change it.
     *
     * The adapter drives the StressState that StressGame owns, handed over by reference (there is no model in a scene). A change of the
     * scene calls @p onSceneChanged, so the game switches the active scene (a test hands a counter instead of a scene manager).
     *
     * State: {"scene", "count", "language", "maxCount", "scenes": ["sprites", "entities", "ui"]}, the names as the flags spell them.
     *
     * Actions (each is accepted by PerformAction as it is):
     * - {"type": "set_scene", "scene": "sprites|entities|ui"}, listed for the scenes that are not active;
     * - {"type": "set_count", "count": N}, listed for the half and the double of the current count inside 1..maxCount;
     * - {"type": "set_language", "language": "en|ru"}, listed for the other language.
     *
     * PerformAction takes any integer `count` in 1..maxCount, not only the listed ones. A malformed or out-of-range value is invalid_params,
     * an action that is well-formed and changes nothing is accepted, illegal_action is only for an unknown `type`. The outcome is
     * {"scene", "count", "language"} after the action.
     *
     * Reset takes an optional {"scene", "count"} (checked like the actions) and puts everything else back to the defaults of StressSettings
     * (English), so a controller starts a measurement from a known state.
     */
    class StressGameControl : public IControllableGame {
    public:
        StressGameControl(StressState& state, std::function<void()> onSceneChanged);

        [[nodiscard]] std::expected<Json, ControlError> GetState() const override;
        [[nodiscard]] std::expected<Json, ControlError> GetActions() const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        void ChangeScene(StressScene scene);

        StressState& _state;
        std::function<void()> _onSceneChanged;
    };

}  // namespace BECore::Stress
