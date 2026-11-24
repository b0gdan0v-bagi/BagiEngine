#pragma once

#include <BECore/Random/Rng.h>
#include <Games/IControllableGame.h>
#include <Games/IGame.h>

namespace BECore::Tests {

    /**
     * @brief A dice game that a recording can replay: the test game of the replay tests.
     *
     * State {"seed", "rolls": [..], "locked"}. Actions {"type": "roll"} (the number comes from Rng(seed)) and {"type": "arm"}
     * (after it `roll` is illegal_action until Update() has let kArmSeconds of game time pass). Reset takes {"seed"?} and draws
     * the seed from a counter when none is given; SetState takes {"rolls"}. Reflected so GameManager::Initialize() creates it
     * (a test that counts the registered games counts it).
     */
    class ReplayTestGame : public IGame, public IControllableGame {
        BE_CLASS(ReplayTestGame)
    public:
        static constexpr double kArmSeconds = 0.5;

        ReplayTestGame() = default;
        ~ReplayTestGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;
        IControllableGame* AsControllable() override;

        std::expected<void, ControlError> WriteState(JsonWriter& writer) const override;
        std::expected<void, ControlError> WriteActions(JsonWriter& writer) const override;
        std::expected<void, ControlError> PerformAction(JsonView action, JsonWriter& outcome) override;
        std::expected<void, ControlError> Reset(JsonView options) override;
        std::expected<void, ControlError> SetState(JsonView state) override;
        uint64_t GetStateVersion() const override;
        bool IsReplayable() const override;
        std::expected<void, ControlError> WriteReplayStart(JsonWriter& writer) const override;

        /// Lets @p seconds of game time pass: the part of the frame that a game's timers run in
        void Update(double seconds);

        /// The next seed a Reset without one takes, and back to the first one
        static void RestartSeedCounter();

    private:
        [[nodiscard]] bool IsLocked() const;

        uint64_t _seed = 0;
        Rng _rng;
        Vector<int> _rolls;
        double _lockSeconds = 0.0;
        uint64_t _version = 0;
    };

}  // namespace BECore::Tests
