#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonTape.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Games/ReplayFile.h>
#include <expected>

namespace BECore {

    class IControllableGame;

    /// How an entry of a replay differs from what the game did
    CORE_ENUM(ReplayMismatchKind, uint8_t,
              Refused,         ///< The game refused a call it had accepted when the file was recorded
              OutcomeDiffers,  ///< The action was accepted and answered something else
              StartDiffers     ///< After a `reset` the game does not start from the params of the entry (another seed)
    );

    /// The name of @p kind in an answer of the control API: `refused`, `outcome_differs`, `start_differs`
    [[nodiscard]] StringView ReplayMismatchKindName(ReplayMismatchKind kind);

    /// The first entry of a replay that the game did not repeat
    struct ReplayMismatch {
        size_t index = 0;   ///< 0-based position among the entries of the file
        ReplayEntry entry;  ///< As the file holds it, `line` included
        String entryText;   ///< The entry as a line of the file (FormatReplayEntry), made once when the mismatch is found
        ReplayMismatchKind kind = ReplayMismatchKind::Refused;
        Optional<ControlError> error;  ///< `Refused`: what the game answered
        Optional<String> expected;     ///< JSON text; `OutcomeDiffers`: the outcome of the file; `StartDiffers`: the params of the entry
        Optional<String> actual;       ///< JSON text; `OutcomeDiffers`: what the game answered; `StartDiffers`: the replay start of the game
    };

    /// The result of applying one entry: success, or the mismatch that ends the replay
    using ReplayStep = std::expected<void, ReplayMismatch>;

    /**
     * @brief Plays a replay file into a game, one entry at a time.
     *
     * The player has no clock and no frames: the caller asks PeekGap() how much game time the next entry waits for, lets that pass
     * (the control method `replay` steps the manual clock, a test advances its own), then calls Apply(). The entries act on the
     * adapter directly, never through SessionRecorder, so playing a file does not record one. The first mismatch ends the replay:
     * IsDone() is true from then on.
     */
    class ReplayPlayer {
    public:
        /// Reads @p text; a file of another game than @p gameName is refused with the line of its header
        [[nodiscard]] std::expected<void, ReplayError> Load(StringView text, StringView gameName);

        [[nodiscard]] size_t GetEntryCount() const;

        /// True when every entry was applied, or after a mismatch
        [[nodiscard]] bool IsDone() const;

        /// The game time the next entry waits for; empty (no frames) once the player is done
        [[nodiscard]] ReplayGap PeekGap() const;

        /// Applies the next entry to @p game; the caller has let PeekGap() pass first and checked !IsDone()
        ReplayStep Apply(IControllableGame& game);

    private:
        ReplayStep ApplyReset(IControllableGame& game, size_t index, const ReplayEntry& entry);
        ReplayStep ApplyAction(IControllableGame& game, size_t index, const ReplayEntry& entry);
        ReplayStep ApplyState(IControllableGame& game, size_t index, const ReplayEntry& entry);

        ParsedReplay _replay;
        size_t _next = 0;
        bool _stopped = false;

        // What one Apply() reads and writes; the texts and tapes keep their capacity from entry to entry, and each tape is parsed again at every use
        JsonTape _paramsTape;    ///< The entry's params, handed to the game
        JsonTape _expectedTape;  ///< The entry's outcome, the file's side of the comparison
        JsonTape _actualTape;    ///< What the game wrote (the outcome, or the replay start)
        String _actualText;
    };

    /**
     * @brief The mismatch as the answer of the control method `replay` carries it, written to JSON text.
     *
     * {index, line, kind, entry (as a line of the file holds it), error? ({code, message}), expected?, actual?}: the reflected `ReplayMismatchAnswer`
     * (ReplayAnswers.h) that the control method answers, for the tests and helpers that read the text. Defined in ReplayAnswers.cpp.
     */
    [[nodiscard]] String ReplayMismatchText(const ReplayMismatch& mismatch);

}  // namespace BECore
