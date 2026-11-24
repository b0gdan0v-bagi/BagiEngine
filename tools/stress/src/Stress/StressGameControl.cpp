#include "StressGameControl.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonWriter.h>

namespace BECore::Stress {

    namespace {

        constexpr StringView kSetScene = "set_scene";
        constexpr StringView kSetCount = "set_count";
        constexpr StringView kSetLanguage = "set_language";

        std::unexpected<ControlError> InvalidParams(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        /// The scene named by member @p key of @p object, or the invalid_params error that says what is wrong with it
        std::expected<StressScene, ControlError> ReadScene(JsonView object, StringView key) {
            const auto name = GetParamString(object, key);
            const auto scene = name ? ParseScene(*name) : std::nullopt;
            if (!scene) {
                return InvalidParams(Format("'{}' has to be one of: sprites, entities, ui", key));
            }
            return *scene;
        }

        std::expected<StressLanguage, ControlError> ReadLanguage(JsonView object, StringView key) {
            const auto name = GetParamString(object, key);
            const auto language = name ? ParseLanguage(*name) : std::nullopt;
            if (!language) {
                return InvalidParams(Format("'{}' has to be one of: en, ru", key));
            }
            return *language;
        }

        /// The count in member @p key of @p object (1..kMaxCount). Checked as int64_t before the narrowing, so a huge number cannot wrap
        std::expected<uint32_t, ControlError> ReadCount(JsonView object, StringView key) {
            const auto count = GetParamInt(object, key);
            if (!count || *count < 1 || *count > static_cast<int64_t>(kMaxCount)) {
                return InvalidParams(Format("'{}' has to be an integer from 1 to {}", key, kMaxCount));
            }
            return static_cast<uint32_t>(*count);
        }

        void WriteShown(JsonWriter& writer, const StressState& state) {
            writer.BeginObject();
            writer.Member("count", state.GetCount());
            writer.Member("language", ToFlagValue(state.GetLanguage()));
            writer.Member("scene", ToFlagValue(state.GetScene()));
            writer.EndObject();
        }

        /**
         * Tells @p visitor each listed action in the order of the list: OnScene(StressScene), OnCount(uint32_t), OnLanguage(StressLanguage). The one
         * place that decides what is listed.
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

    std::expected<void, ControlError> StressGameControl::PerformAction(JsonView action, JsonWriter& outcome) {
        const auto type = GetParamString(action, "type");
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
        WriteShown(outcome, _state);
        return {};
    }

    std::expected<void, ControlError> StressGameControl::Reset(JsonView options) {
        const StressSettings defaults;
        StressScene scene = defaults.scene;
        uint32_t count = defaults.count;

        // Everything is checked before anything changes: a bad option leaves the state as it was
        if (FindParam(options, "scene").has_value()) {
            const auto requested = ReadScene(options, "scene");
            if (!requested) {
                return std::unexpected(requested.error());
            }
            scene = *requested;
        }
        if (FindParam(options, "count").has_value()) {
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
