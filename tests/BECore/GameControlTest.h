#pragma once

#include <Games/IControllableGame.h>
#include <Games/IGame.h>
#include <cstdint>

namespace BECore::Tests {

    /**
     * @brief A game that a controller can drive: a counter with one action, "inc".
     *
     * State {"counter"}, actions [{"type": "inc"}], Reset takes {"start"}, SetState takes {"counter"}. It derives
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

        std::expected<Json, ControlError> GetState() const override;
        std::expected<Json, ControlError> GetActions() const override;
        std::expected<void, ControlError> WriteState(JsonWriter& writer) const override;
        std::expected<void, ControlError> WriteActions(JsonWriter& writer) const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        std::expected<void, ControlError> SetState(const Json& state) override;
        uint64_t GetStateVersion() const override;

        /// Adds @p amount to the counter and moves the version: the reflected method the editor's Game panel calls in its test
        BE_FUNCTION void AddToCounter(int32_t amount);

        int counter = 0;
        uint64_t version = 0;
        int plainResetCalls = 0;  // IGame::Reset() calls

        /// The writing forms: by default the ones of IControllableGame (through GetState / GetActions). With `handWritten` they write the same
        /// documents by hand and count the calls; with `failWrites` they write half an object and fail (GameControlWriterTest.cpp)
        bool handWritten = false;
        bool failWrites = false;
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
