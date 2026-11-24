#include "ReplayRequest.h"

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonWriter.h>

namespace BECore {

    namespace {

        constexpr StringView kKeyId = "id";
        constexpr StringView kKeyMethod = "method";
        constexpr StringView kKeyParams = "params";
        constexpr StringView kKeyPath = "path";
        constexpr StringView kMethodReplay = "replay";
        constexpr StringView kKeyResult = "result";
        constexpr StringView kKeyError = "error";
        constexpr StringView kKeyOk = "ok";
        constexpr StringView kKeyEntries = "entries";
        constexpr StringView kKeyCode = "code";
        constexpr StringView kKeyMismatch = "mismatch";

    }  // namespace

    String MakeReplayRequestText(StringView path) {
        String text;
        JsonWriter writer(text);
        writer.BeginObject();
        writer.Member(kKeyId, kReplayRequestId);
        writer.Member(kKeyMethod, kMethodReplay);
        writer.Key(kKeyParams);
        writer.BeginObject();
        writer.Member(kKeyPath, path);
        writer.EndObject();
        writer.EndObject();
        return text;
    }

    ReplayOutcome ReadReplayAnswer(StringView text) {
        ReplayOutcome outcome;
        outcome.detail = String(text);
        JsonTape tape;
        if (!tape.Parse(text)) {
            return outcome;
        }
        const JsonView answer(tape, 0);
        const Optional<JsonView> result = FindParam(answer, kKeyResult);
        const Optional<JsonView> error = FindParam(answer, kKeyError);
        if (result && GetParamBool(*result, kKeyOk).value_or(false)) {
            outcome.kind = ReplayOutcomeKind::Played;
            outcome.entries = GetParamInt(*result, kKeyEntries).value_or(0);
            outcome.detail.clear();
            return outcome;
        }
        if (error && GetParamString(*error, kKeyCode).value_or(String()) == ControlErrors::kShuttingDown) {
            outcome.kind = ReplayOutcomeKind::ShuttingDown;
            outcome.detail.clear();
            return outcome;
        }
        const Optional<JsonView> mismatch = result ? FindParam(*result, kKeyMismatch) : std::nullopt;
        const Optional<JsonView> named = mismatch ? mismatch : error;
        outcome.isMismatch = mismatch.has_value();
        if (named) {
            const StringView raw = named->Raw();
            outcome.detail = String(raw.data(), raw.size());
        }
        return outcome;
    }

}  // namespace BECore
