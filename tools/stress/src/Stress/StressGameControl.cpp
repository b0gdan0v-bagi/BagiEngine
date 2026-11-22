#include "StressGameControl.h"

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>

namespace BECore::Stress {

    namespace {

        constexpr StringView kSetScene = "set_scene";
        constexpr StringView kSetCount = "set_count";
        constexpr StringView kSetLanguage = "set_language";

        std::unexpected<ControlError> InvalidParams(String message) {
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
        std::expected<StressScene, ControlError> ReadScene(const Json& object, StringView key) {
            const auto name = GetJsonString(object, key);
            const auto scene = name ? ParseScene(*name) : std::nullopt;
            if (!scene) {
                return InvalidParams(Format("'{}' has to be one of: sprites, entities, ui", key));
            }
            return *scene;
        }

        std::expected<StressLanguage, ControlError> ReadLanguage(const Json& object, StringView key) {
            const auto name = GetJsonString(object, key);
            const auto language = name ? ParseLanguage(*name) : std::nullopt;
            if (!language) {
                return InvalidParams(Format("'{}' has to be one of: en, ru", key));
            }
            return *language;
        }

        /// The count in member @p key of @p object (1..kMaxCount). Checked as int64_t before the narrowing, so a huge number cannot wrap
        std::expected<uint32_t, ControlError> ReadCount(const Json& object, StringView key) {
            const auto count = GetJsonInt(object, key);
            if (!count || *count < 1 || *count > static_cast<int64_t>(kMaxCount)) {
                return InvalidParams(Format("'{}' has to be an integer from 1 to {}", key, kMaxCount));
            }
            return static_cast<uint32_t>(*count);
        }

        Json DescribeAction(StringView type, StringView key, Json value) {
            Json action = Json::object();
            action["type"] = MakeJsonString(type);
            action[ToStdString(key)] = std::move(value);
            return action;
        }

        Json DescribeShown(const StressState& state) {
            Json shown = Json::object();
            shown["scene"] = MakeJsonString(ToFlagValue(state.GetScene()));
            shown["count"] = state.GetCount();
            shown["language"] = MakeJsonString(ToFlagValue(state.GetLanguage()));
            return shown;
        }

        /**
         * Tells @p visitor each listed action in the order of the list: OnScene(StressScene), OnCount(uint32_t), OnLanguage(StressLanguage). The one
         * place that decides what is listed, so the Json and the writing form cannot drift apart.
         */
        template <typename TVisitor>
        void VisitActions(const StressState& state, TVisitor& visitor) {
            for (const StressScene scene : EnumUtils<StressScene>::Values()) {
                if (scene != state.GetScene()) {
                    visitor.OnScene(scene);
                }
            }

            const uint32_t count = state.GetCount();
            const uint32_t half = count / 2;
            const uint64_t twice = static_cast<uint64_t>(count) * 2;
            if (half >= 1 && half != count) {
                visitor.OnCount(half);
            }
            if (count >= 1 && twice <= kMaxCount) {
                visitor.OnCount(static_cast<uint32_t>(twice));
            }

            visitor.OnLanguage(state.GetLanguage() == StressLanguage::En ? StressLanguage::Ru : StressLanguage::En);
        }

        struct ActionsToJson {
            void OnScene(StressScene scene) {
                actions.push_back(DescribeAction(kSetScene, "scene", MakeJsonString(ToFlagValue(scene))));
            }
            void OnCount(uint32_t count) {
                actions.push_back(DescribeAction(kSetCount, "count", count));
            }
            void OnLanguage(StressLanguage language) {
                actions.push_back(DescribeAction(kSetLanguage, "language", MakeJsonString(ToFlagValue(language))));
            }

            Json actions = Json::array();
        };

        /// An action is an object with its keys in alphabetical order: the argument, then `type`
        struct ActionsToWriter {
            void OnScene(StressScene scene) {
                writer.BeginObject();
                writer.Member("scene", ToFlagValue(scene));
                writer.Member("type", kSetScene);
                writer.EndObject();
            }
            void OnCount(uint32_t count) {
                writer.BeginObject();
                writer.Member("count", count);
                writer.Member("type", kSetCount);
                writer.EndObject();
            }
            void OnLanguage(StressLanguage language) {
                writer.BeginObject();
                writer.Member("language", ToFlagValue(language));
                writer.Member("type", kSetLanguage);
                writer.EndObject();
            }

            JsonWriter& writer;
        };

    }  // namespace

    StressGameControl::StressGameControl(StressState& state, MoveOnlyFunction<void()> onSceneChanged) : _state(state), _onSceneChanged(std::move(onSceneChanged)) {}

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
        ActionsToJson visitor;
        VisitActions(_state, visitor);
        return std::move(visitor.actions);
    }

    std::expected<void, ControlError> StressGameControl::WriteState(JsonWriter& writer) const {
        writer.BeginObject();
        writer.Member("count", _state.GetCount());
        writer.Member("language", ToFlagValue(_state.GetLanguage()));
        writer.Member("maxCount", kMaxCount);
        writer.Member("scene", ToFlagValue(_state.GetScene()));
        writer.Key("scenes");
        writer.BeginArray();
        for (const StressScene scene : EnumUtils<StressScene>::Values()) {
            writer.Value(ToFlagValue(scene));
        }
        writer.EndArray();
        writer.EndObject();
        return {};
    }

    std::expected<void, ControlError> StressGameControl::WriteActions(JsonWriter& writer) const {
        writer.BeginArray();
        ActionsToWriter visitor{writer};
        VisitActions(_state, visitor);
        writer.EndArray();
        return {};
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
