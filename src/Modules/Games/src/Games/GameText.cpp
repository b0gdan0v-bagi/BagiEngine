#include "GameText.h"

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <Games/IControllableGame.h>

namespace BECore {

    namespace {

        using WriteFn = FunctionRef<std::expected<void, ControlError>(JsonWriter&)>;

        std::expected<String, ControlError> WriteText(WriteFn write) {
            String text;
            JsonWriter writer(text);
            if (auto written = write(writer); !written) {
                return std::unexpected(std::move(written.error()));
            }
            if (!writer.IsComplete()) {
                return std::unexpected(MakeControlError(ControlErrors::kInternalError, "the game wrote no complete value"));
            }
            return text;
        }

        /// Tapes @p json and calls @p call with its root
        template <typename TCall>
        auto WithView(StringView json, TCall&& call) -> decltype(call(JsonView())) {
            JsonTape tape;
            if (const auto parsed = tape.Parse(json); !parsed) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("not JSON: {}", parsed.error().message)));
            }
            return call(JsonView(tape, 0));
        }

    }  // namespace

    std::expected<String, ControlError> StateText(const IControllableGame& game) {
        return WriteText([&](JsonWriter& writer) { return game.WriteState(writer); });
    }

    std::expected<String, ControlError> ActionsText(const IControllableGame& game) {
        return WriteText([&](JsonWriter& writer) { return game.WriteActions(writer); });
    }

    std::expected<String, ControlError> ReplayStartText(const IControllableGame& game) {
        return WriteText([&](JsonWriter& writer) { return game.WriteReplayStart(writer); });
    }

    std::expected<String, ControlError> PerformActionText(IControllableGame& game, StringView actionJson) {
        return WithView(actionJson, [&](JsonView action) { return WriteText([&](JsonWriter& writer) { return game.PerformAction(action, writer); }); });
    }

    std::expected<void, ControlError> ResetText(IControllableGame& game, StringView optionsJson) {
        return WithView(optionsJson, [&](JsonView options) { return game.Reset(options); });
    }

    std::expected<void, ControlError> SetStateText(IControllableGame& game, StringView stateJson) {
        return WithView(stateJson, [&](JsonView state) { return game.SetState(state); });
    }

}  // namespace BECore
