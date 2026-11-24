#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    /// The id of the one request the application posts to itself, so it needs no other meaning
    inline constexpr int64_t kReplayRequestId = 1;

    /// The text of the `replay` request for the file @p path, keys in the order the DOM wrote them (`id`, `method`, `params`)
    [[nodiscard]] String MakeReplayRequestText(StringView path);

    CORE_ENUM(ReplayOutcomeKind, uint8_t, Played, ShuttingDown, Failed)

    /// What the application does with the answer of a replay
    struct ReplayOutcome {
        ReplayOutcomeKind kind = ReplayOutcomeKind::Failed;

        /// Played: the entries of the file that were played
        int64_t entries = 0;

        /// Failed: the answer reported a mismatch of the replay, not another error
        bool isMismatch = false;

        /// Failed: the text of the `result.mismatch` member, else of `error`, else the whole answer
        String detail;
    };

    /// Reads the answer text of a `replay` request; text that is not an answer is a failure whose detail is the text
    [[nodiscard]] ReplayOutcome ReadReplayAnswer(StringView text);

}  // namespace BECore
