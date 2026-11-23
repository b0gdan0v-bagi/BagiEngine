#include "ReplayAnswers.h"

#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <Games/ReplayFile.h>
#include <Games/ReplayPlayer.h>
#include <Generated/ReplayAnswers.gen.hpp>

namespace BECore {

    void FillReplayMismatchAnswer(ReplayMismatchAnswer& answer, const ReplayMismatch& mismatch) {
        answer.index = mismatch.index;
        answer.line = mismatch.entry.line;
        answer.kind = TextView(ReplayMismatchKindName(mismatch.kind));
        answer.entry = JsonPayload::Text(StringView(mismatch.entryText.c_str(), mismatch.entryText.size()));
        if (mismatch.error) {
            ControlErrorAnswer error;
            error.code = TextView(mismatch.error->code);
            error.message = TextView(mismatch.error->message);
            answer.error = std::move(error);
        }
        if (mismatch.expected) {
            answer.expected = JsonPayload::Text(StringView(mismatch.expected->c_str(), mismatch.expected->size()));
        }
        if (mismatch.actual) {
            answer.actual = JsonPayload::Text(StringView(mismatch.actual->c_str(), mismatch.actual->size()));
        }
    }

    String ReplayMismatchText(const ReplayMismatch& mismatch) {
        ReplayMismatchAnswer answer;
        FillReplayMismatchAnswer(answer, mismatch);
        String text;
        JsonWriter writer(text);
        answer.WriteJson(writer);
        return text;
    }

}  // namespace BECore
