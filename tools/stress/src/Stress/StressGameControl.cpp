#include "StressGameControl.h"

#include <BECore/Control/Json.h>

namespace BECore::Stress {

    namespace {

        constexpr eastl::string_view kSetScene = "set_scene";
        constexpr eastl::string_view kSetCount = "set_count";
        constexpr eastl::string_view kSetLanguage = "set_language";

        std::unexpected<ControlError> InvalidParams(eastl::string message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        Json SceneNames() {
            Json names = Json::array();
            for (const StressScene scene : EnumUtils<StressScene>::Values()) {
                names.push_back(MakeJsonString(ToFlagValue(scene)));
            }
            return names;
        }

        /// The scene named by member @p key of @p object, or the invalid_params error that says what is wrong with it
        std::expected<StressScene, ControlError> ReadScene(const Json& object, eastl::string_view key) {
            const auto name = GetJsonString(object, key);
            const auto scene = name ? ParseScene(*name) : std::nullopt;
            if (!scene) {
                return InvalidParams(Format("'{}' has to be one of: sprites, entities, ui", key));
            }
            return *scene;
        }

        std::expected<StressLanguage, ControlError> ReadLanguage(const Json& object, eastl::string_view key) {
            const auto name = GetJsonString(object, key);
            const auto language = name ? ParseLanguage(*name) : std::nullopt;
            if (!language) {
                return InvalidParams(Format("'{}' has to be one of: en, ru", key));
            }
            return *language;
        }

        /// The count in member @p key of @p object (1..kMaxCount). Checked as int64_t before the narrowing, so a huge number cannot wrap
        std::expected<uint32_t, ControlError> ReadCount(const Json& object, eastl::string_view key) {
            const auto count = GetJsonInt(object, key);
            if (!count || *count < 1 || *count > static_cast<int64_t>(kMaxCount)) {
                return InvalidParams(Format("'{}' has to be an integer from 1 to {}", key, kMaxCount));
            }
            return static_cast<uint32_t>(*count);
        }

        Json DescribeAction(eastl::string_view type, eastl::string_view key, Json value) {
            Json action = Json::object();
            action["type"] = MakeJsonString(type);
            action[std::string(key.data(), key.size())] = std::move(value);
            return action;
        }

        Json DescribeShown(const StressState& state) {
            Json shown = Json::object();
            shown["scene"] = MakeJsonString(ToFlagValue(state.GetScene()));
            shown["count"] = state.GetCount();
            shown["language"] = MakeJsonString(ToFlagValue(state.GetLanguage()));
            return shown;
        }

    }  // namespace

    StressGameControl::StressGameControl(StressState& state, std::function<void()> onSceneChanged) : _state(state), _onSceneChanged(std::move(onSceneChanged)) {}

    void StressGameControl::ChangeScene(StressScene scene) {
        if (scene == _state.GetScene()) {
            return;
        }
        _state.SetScene(scene);
        if (_onSceneChanged) {
            _onSceneChanged();
        }
    }

    std::expected<Json, ControlError> StressGameControl::GetState() const {
        Json state = DescribeShown(_state);
        state["maxCount"] = kMaxCount;
        state["scenes"] = SceneNames();
        return state;
    }

    std::expected<Json, ControlError> StressGameControl::GetActions() const {
        Json actions = Json::array();
        for (const StressScene scene : EnumUtils<StressScene>::Values()) {
            if (scene != _state.GetScene()) {
                actions.push_back(DescribeAction(kSetScene, "scene", MakeJsonString(ToFlagValue(scene))));
            }
        }

        const uint32_t count = _state.GetCount();
        const uint32_t half = count / 2;
        const uint64_t twice = static_cast<uint64_t>(count) * 2;
        if (half >= 1 && half != count) {
            actions.push_back(DescribeAction(kSetCount, "count", half));
        }
        if (count >= 1 && twice <= kMaxCount) {
            actions.push_back(DescribeAction(kSetCount, "count", static_cast<uint32_t>(twice)));
        }

        const StressLanguage other = _state.GetLanguage() == StressLanguage::En ? StressLanguage::Ru : StressLanguage::En;
        actions.push_back(DescribeAction(kSetLanguage, "language", MakeJsonString(ToFlagValue(other))));
        return actions;
    }

    std::expected<Json, ControlError> StressGameControl::PerformAction(const Json& action) {
        const auto type = GetJsonString(action, "type");
        if (!type) {
            return InvalidParams("'type' has to be a string");
        }

        if (*type == kSetScene) {
            const auto scene = ReadScene(action, "scene");
            if (!scene) {
                return std::unexpected(scene.error());
            }
            ChangeScene(*scene);
        } else if (*type == kSetCount) {
            const auto count = ReadCount(action, "count");
            if (!count) {
                return std::unexpected(count.error());
            }
            _state.SetCount(*count);
        } else if (*type == kSetLanguage) {
            const auto language = ReadLanguage(action, "language");
            if (!language) {
                return std::unexpected(language.error());
            }
            _state.SetLanguage(*language);
        } else {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, Format("unknown action type '{}'", *type)));
        }
        return DescribeShown(_state);
    }

    std::expected<void, ControlError> StressGameControl::Reset(const Json& options) {
        const StressSettings defaults;
        StressScene scene = defaults.scene;
        uint32_t count = defaults.count;

        // Everything is checked before anything changes: a bad option leaves the state as it was
        if (FindJsonMember(options, "scene") != nullptr) {
            const auto requested = ReadScene(options, "scene");
            if (!requested) {
                return std::unexpected(requested.error());
            }
            scene = *requested;
        }
        if (FindJsonMember(options, "count") != nullptr) {
            const auto requested = ReadCount(options, "count");
            if (!requested) {
                return std::unexpected(requested.error());
            }
            count = *requested;
        }

        ChangeScene(scene);
        _state.SetCount(count);
        _state.SetLanguage(defaults.language);
        return {};
    }

    uint64_t StressGameControl::GetStateVersion() const {
        return _state.Version();
    }

}  // namespace BECore::Stress
