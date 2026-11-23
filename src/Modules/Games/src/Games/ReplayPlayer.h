#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/Json.h>
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
        ReplayMismatchKind kind = ReplayMismatchKind::Refused;
        Optional<ControlError> error;  ///< `Refused`: what the game answered
        Optional<Json> expected;       ///< `OutcomeDiffers`: the outcome of the file; `StartDiffers`: the params of the entry
        Optional<Json> actual;         ///< `OutcomeDiffers`: what the game answered; `StartDiffers`: IControllableGame::GetReplayStart()
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
        ParsedReplay _replay;
        size_t _next = 0;
        bool _stopped = false;
    };

    /**
     * @brief The mismatch as the answer of the control method `replay` carries it.
     *
     * {index, line, kind, entry (as a line of the file holds it), error? ({code, message}), expected?, actual?}. A DOM on purpose: the entry's params and the two outcomes
     * are documents of the game that sit between its own keys.
     */
    [[nodiscard]] Json ReplayMismatchJson(const ReplayMismatch& mismatch);

}  // namespace BECore
