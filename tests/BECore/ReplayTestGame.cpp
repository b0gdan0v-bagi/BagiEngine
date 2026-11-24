#include "ReplayTestGame.h"

#include <BECore/Control/ControlParams.h>
#include <Generated/ReplayTestGame.gen.hpp>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kFirstDrawnSeed = 1000;
        constexpr int kDieFaces = 6;
        constexpr double kLockEpsilon = 1e-9;  // a lock of 0.5 s run down by frames of a replay ends at "almost zero"

        uint64_t nextDrawnSeed = kFirstDrawnSeed;

    }  // namespace

    PoolString ReplayTestGame::GetName() const {
        return "ReplayTest"_intern;
    }

    PoolString ReplayTestGame::GetSceneName() const {
        return PoolString();
    }

    IControllableGame* ReplayTestGame::AsControllable() {
        return this;
    }

    bool ReplayTestGame::IsLocked() const {
        return _lockSeconds > kLockEpsilon;
    }

    std::expected<void, ControlError> ReplayTestGame::WriteState(JsonWriter& writer) const {
        writer.BeginObject();
        writer.Member("locked", IsLocked());
        writer.Key("rolls");
        writer.BeginArray();
        for (const int roll : _rolls) {
            writer.Value(roll);
        }
        writer.EndArray();
        writer.Member("seed", _seed);
        writer.EndObject();
        return {};
    }

    std::expected<void, ControlError> ReplayTestGame::WriteActions(JsonWriter& writer) const {
        writer.BeginArray();
        if (!IsLocked()) {
            writer.BeginObject();
            writer.Member("type", "roll");
            writer.EndObject();
        }
        writer.BeginObject();
        writer.Member("type", "arm");
        writer.EndObject();
        writer.EndArray();
        return {};
    }

    std::expected<void, ControlError> ReplayTestGame::PerformAction(JsonView action, JsonWriter& outcome) {
        const auto type = GetParamString(action, "type");
        if (!type) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be a string"));
        }
        if (*type == "roll") {
            if (IsLocked()) {
                return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "the dice are locked"));
            }
            const int value = _rng.UniformInt(1, kDieFaces);
            _rolls.push_back(value);
            outcome.BeginObject();
            outcome.Member("value", value);
            outcome.EndObject();
        } else if (*type == "arm") {
            if (IsLocked()) {
                return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "the dice are already locked"));
            }
            _lockSeconds = kArmSeconds;
            outcome.BeginObject();
            outcome.Member("locked", true);
            outcome.EndObject();
        } else {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "only \"roll\" and \"arm\" are legal"));
        }
        ++_version;
        return {};
    }

    std::expected<void, ControlError> ReplayTestGame::Reset(JsonView options) {
        uint64_t seed = 0;
        if (FindParam(options, "seed")) {
            const auto requested = GetParamInt(options, "seed");
            if (!requested || *requested < 0) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'seed' has to be a non-negative integer"));
            }
            seed = static_cast<uint64_t>(*requested);
        } else {
            seed = nextDrawnSeed++;
        }
        _seed = seed;
        _rng = Rng(seed);
        _rolls.clear();
        _lockSeconds = 0.0;
        ++_version;
        return {};
    }

    std::expected<void, ControlError> ReplayTestGame::SetState(JsonView state) {
        const Optional<JsonView> rolls = FindParam(state, "rolls");
        if (!rolls || !rolls->IsArray()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'rolls' has to be an array"));
        }
        Vector<int> loaded;
        for (const JsonView roll : rolls->Items()) {
            const Optional<int64_t> value = roll.AsInt64();
            if (!value || *value < 1 || *value > kDieFaces) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "every roll has to be an integer from 1 to 6"));
            }
            loaded.push_back(static_cast<int>(*value));
        }
        _rolls = std::move(loaded);
        ++_version;
        return {};
    }

    uint64_t ReplayTestGame::GetStateVersion() const {
        return _version;
    }

    bool ReplayTestGame::IsReplayable() const {
        return true;
    }

    std::expected<void, ControlError> ReplayTestGame::WriteReplayStart(JsonWriter& writer) const {
        writer.BeginObject();
        writer.Member("seed", _seed);
        writer.EndObject();
        return {};
    }

    void ReplayTestGame::Update(double seconds) {
        if (_lockSeconds > 0.0) {
            _lockSeconds = std::max(0.0, _lockSeconds - seconds);
        }
    }

    void ReplayTestGame::RestartSeedCounter() {
        nextDrawnSeed = kFirstDrawnSeed;
    }

}  // namespace BECore::Tests
