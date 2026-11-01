#pragma once

#include <Games/IControllableGame.h>
#include <Games/IGame.h>

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
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        std::expected<void, ControlError> SetState(const Json& state) override;
        uint64_t GetStateVersion() const override;

        int counter = 0;
        uint64_t version = 0;
        int plainResetCalls = 0;  // IGame::Reset() calls
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
