#pragma once

#include <Games/IControllableGame.h>
#include <Games/IGame.h>
#include <cstdint>

namespace BECore::Tests {

    /**
     * @brief A game that a controller can drive: a counter with one action, "inc".
     *
     * State {"counter"}, actions [{"type": "inc"}], the outcome of "inc" {"counter"}, Reset takes {"start"}, SetState takes {"counter"}. It derives
     * from IGame first and from IControllableGame second, which the reflection generator allows (it reads the first base).
     * Reflected so GameManager::Initialize() creates it through AbstractFactory<IGame> (see GameControlTest.cpp).
     */
    class FakeControllableGame : public IGame, public IControllableGame {
        BE_CLASS(FakeControllableGame)
    public:
        FakeControllableGame() = default;
        ~FakeControllableGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;
        void Reset() override;
        IControllableGame* AsControllable() override;

        std::expected<void, ControlError> WriteState(JsonWriter& writer) const override;
        std::expected<void, ControlError> WriteActions(JsonWriter& writer) const override;
        std::expected<void, ControlError> PerformAction(JsonView action, JsonWriter& outcome) override;
        std::expected<void, ControlError> Reset(JsonView options) override;
        std::expected<void, ControlError> SetState(JsonView state) override;
        uint64_t GetStateVersion() const override;

        /// Adds @p amount to the counter and moves the version: the reflected method the editor's Game panel calls in its test
        BE_FUNCTION void AddToCounter(int32_t amount);

        int counter = 0;
        uint64_t version = 0;
        int plainResetCalls = 0;  // IGame::Reset() calls

        /// With `failWrites` the writing forms write half a value and fail (GameControlWriterTest.cpp); with `rawState` not empty `WriteState` writes
        /// that text as the state (one JSON value), for a state with numbers and escapes the counter cannot make
        bool failWrites = false;
        bool silentAction = false;  // `PerformAction` accepts and writes no outcome: a game that breaks the contract
        String rawState;
        mutable int writeStateCalls = 0;
        mutable int writeActionsCalls = 0;
    };

    /// A game that cannot be controlled from outside: it counts the IGame::Reset() calls
    class FakePlainGame : public IGame {
        BE_CLASS(FakePlainGame)
    public:
        FakePlainGame() = default;
        ~FakePlainGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;
        void Reset() override;

        int resetCalls = 0;
    };

}  // namespace BECore::Tests
