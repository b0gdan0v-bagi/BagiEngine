#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// The command line flag that records the active game: `--record=FILE`
    inline constexpr StringView kRecordArgument = "--record";

    /**
     * @brief Registers the methods of the replay file in @p service: `record_start`, `record_stop` and `replay`.
     *
     * `record_start` ({path, title?} to {path, game}) starts SessionRecorder on the active game; its errors are no_active_game,
     * not_controllable, unsupported (the game is not replayable) and invalid_params (a recording runs, or the path cannot be
     * written). `record_stop` answers {entries} and fails with invalid_params when no recording runs. The path is a path of
     * the host the game runs on.
     *
     * `replay` ({path} to {ok, entries, frames} and the clock of `step`) plays a file into the active game on the manual clock: for every entry it steps
     * the gap the file holds and applies the entry. A mismatch is a result, {ok: false, mismatch: {...}, entries, frames} (ReplayMismatchAnswer), not an
     * error, and the first one ends the replay. Errors: invalid_params (no path, a file that cannot be read or parsed with its line, a file of another game,
     * a recording is running), no_active_game, not_controllable, unsupported (the game is not replayable) and replay_needs_manual_clock. The path is a
     * path of the host. RegisterGameControl() calls this.
     */
    void RegisterReplayControl(ControlService& service);

    /**
     * @brief Starts the recording that `--record=FILE` asks for, on the active game; does nothing without the flag.
     *
     * A value that is missing or empty, and a game that cannot record, are one log error and no recording: a person who started
     * the game for something else does not lose the session over a flag. GameManager::Initialize() calls this once the default
     * game is active.
     */
    void StartRecordingFromCommandLine();

}  // namespace BECore
