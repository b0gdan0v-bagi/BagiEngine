#include "ReplayAnswers.h"

#include <BECore/Control/Json.h>
#include <Games/ReplayFile.h>
#include <Games/ReplayPlayer.h>
#include <Generated/ReplayAnswers.gen.hpp>

namespace BECore {

    void FillReplayMismatchAnswer(ReplayMismatchAnswer& answer, const ReplayMismatch& mismatch) {
        answer.index = mismatch.index;
        answer.line = mismatch.entry.line;
        answer.kind = String(ReplayMismatchKindName(mismatch.kind));
        if (auto entry = ParseJson(FormatReplayEntry(mismatch.entry))) {
            answer.entry = JsonPayload::Owned(std::move(*entry));
        }
        if (mismatch.error) {
            ControlErrorAnswer error;
            error.code = mismatch.error->code;
            error.message = mismatch.error->message;
            answer.error = std::move(error);
        }
        if (mismatch.expected) {
            answer.expected = JsonPayload::Borrowed(*mismatch.expected);
        }
        if (mismatch.actual) {
            answer.actual = JsonPayload::Borrowed(*mismatch.actual);
        }
    }

}  // namespace BECore
