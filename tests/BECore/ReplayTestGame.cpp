#include "ReplayTestGame.h"

#include <BECore/Control/Json.h>
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

    std::expected<Json, ControlError> ReplayTestGame::GetState() const {
        Json state = Json::object();
        state["seed"] = _seed;
        state["rolls"] = Json::array();
        for (const int roll : _rolls) {
            state["rolls"].push_back(roll);
        }
        state["locked"] = IsLocked();
        return state;
    }

    std::expected<Json, ControlError> ReplayTestGame::GetActions() const {
        Json actions = Json::array();
        Json roll = Json::object();
        roll["type"] = "roll";
        Json arm = Json::object();
        arm["type"] = "arm";
        if (!IsLocked()) {
            actions.push_back(roll);
        }
        actions.push_back(arm);
        return actions;
    }

    std::expected<Json, ControlError> ReplayTestGame::PerformAction(const Json& action) {
        const auto type = GetJsonString(action, "type");
        if (!type) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'type' has to be a string"));
        }
        Json outcome = Json::object();
        if (*type == "roll") {
            if (IsLocked()) {
                return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "the dice are locked"));
            }
            const int value = _rng.UniformInt(1, kDieFaces);
            _rolls.push_back(value);
            outcome["value"] = value;
        } else if (*type == "arm") {
            if (IsLocked()) {
                return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "the dice are already locked"));
            }
            _lockSeconds = kArmSeconds;
            outcome["locked"] = true;
        } else {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, "only \"roll\" and \"arm\" are legal"));
        }
        ++_version;
        return outcome;
    }

    std::expected<void, ControlError> ReplayTestGame::Reset(const Json& options) {
        uint64_t seed = 0;
        if (FindJsonMember(options, "seed") != nullptr) {
            const auto requested = GetJsonInt(options, "seed");
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

    std::expected<void, ControlError> ReplayTestGame::SetState(const Json& state) {
        const Json* rolls = FindJsonMember(state, "rolls");
        if (rolls == nullptr || !rolls->is_array()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'rolls' has to be an array"));
        }
        Vector<int> loaded;
        for (const Json& roll : *rolls) {
            if (!roll.is_number_integer() || roll.get<int64_t>() < 1 || roll.get<int64_t>() > kDieFaces) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "every roll has to be an integer from 1 to 6"));
            }
            loaded.push_back(static_cast<int>(roll.get<int64_t>()));
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

    Json ReplayTestGame::GetReplayStart() const {
        Json start = Json::object();
        start["seed"] = _seed;
        return start;
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
