#pragma once

#include <BECore/Control/JsonViewOps.h>
#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <BECore/Format/Format.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Time/Time.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <Common/Printers.h>
#include <Games/GameText.h>
#include <Games/IControllableGame.h>
#include <Games/IGame.h>
#include <Games/ReplayPlayer.h>
#include <expected>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /// One frame of game time: what a replay passes for each frame of a gap. @p seconds is the frame time.
    using ReplayStepFn = FunctionRef<void(double seconds)>;

    /**
     * The default frame of PlayReplayFile: the clock advances, and the active scene updates when there is one.
     * A game whose rules also run elsewhere (timers of its own, a model that is not in the scene) passes its own function
     * that does the same and that too.
     */
    inline void StepReplayFrame(double seconds) {
        CoreManager::GetTime().Advance(seconds);
        const SceneManager* scenes = CoreManager::FindSceneManager();
        if (scenes != nullptr && scenes->GetActiveScene() != nullptr) {
            scenes->UpdateAll();
        }
    }

    /// Why a replay file did not play through: a message, and the mismatch when the game was the reason
    struct ReplayCheckFailure {
        String message;
        Optional<ReplayMismatch> mismatch;
    };

    /**
     * Plays the replay file at @p path into @p game, the way the control method `replay` does: for each entry the gap passes (@p step once per
     * frame, `seconds / frames` each), then the entry is applied. Returns the first mismatch or the reason the file could not be used (it cannot be
     * read, does not parse, or is of another game than @p gameName). The file is read-only data of the repository, found by the caller from
     * `__FILE__`. @p game has to be in the state a fresh start has: the file begins with a `reset`.
     */
    inline std::expected<void, ReplayCheckFailure> PlayReplayFile(IControllableGame& game, StringView gameName, const Path& path, ReplayStepFn step = StepReplayFrame) {
        const auto text = ReadTextFile(path);
        if (!text) {
            return std::unexpected(ReplayCheckFailure{Format("cannot read the replay file '{}': {}", path.View(), text.error()), std::nullopt});
        }

        ReplayPlayer player;
        if (const auto loaded = player.Load(*text, gameName); !loaded) {
            return std::unexpected(ReplayCheckFailure{Format("'{}': {}", path.View(), DescribeReplayError(loaded.error())), std::nullopt});
        }
        while (!player.IsDone()) {
            const ReplayGap gap = player.PeekGap();
            if (gap.frames > 0) {
                const double seconds = gap.seconds / static_cast<double>(gap.frames);
                for (uint64_t frame = 0; frame < gap.frames; ++frame) {
                    step(seconds);
                }
            }
            if (const ReplayStep applied = player.Apply(game); !applied) {
                const ReplayMismatch& mismatch = applied.error();
                return std::unexpected(ReplayCheckFailure{
                    Format("'{}': entry {} (line {}) {}: {}", path.View(), mismatch.index, mismatch.entry.line, ReplayMismatchKindName(mismatch.kind), ReplayMismatchText(mismatch)), mismatch});
            }
        }
        return {};
    }

    /**
     * Plays the replay file into @p game and fails the test unless it plays through and ends in @p expectedStateJson (the JSON text of the state:
     * the state `WriteState()` writes and it are equal as values, whatever the key order). A failure names the entry, its line in the file and what differed.
     * Use through EXPECT_REPLAY_FINAL_STATE.
     */
    inline void ExpectReplayFinalState(IGame& game, const Path& path, StringView expectedStateJson, ReplayStepFn step = StepReplayFrame) {
        IControllableGame* control = game.AsControllable();
        if (control == nullptr) {
            ADD_FAILURE() << Format("the game '{}' cannot be controlled from outside", game.GetName().ToStringView()).c_str();
            return;
        }
        if (const auto played = PlayReplayFile(*control, game.GetName().ToStringView(), path, step); !played) {
            ADD_FAILURE() << played.error().message.c_str();
            return;
        }
        const auto state = StateText(*control);
        if (!state) {
            ADD_FAILURE() << "WriteState failed after the replay: " << state.error().message.c_str();
            return;
        }
        const String& actualText = *state;
        JsonTape actualTape;
        JsonTape expectedTape;
        if (!actualTape.Parse(StringView(actualText.c_str(), actualText.size())) || !expectedTape.Parse(expectedStateJson)) {
            ADD_FAILURE() << "the expected state is not JSON: " << String(expectedStateJson.data(), expectedStateJson.size()).c_str();
            return;
        }
        EXPECT_TRUE(JsonEquals(JsonView(actualTape, 0), JsonView(expectedTape, 0)))
            << "the state after '" << path.CStr() << "'\n actual:   " << actualText.c_str() << "\n expected: " << String(expectedStateJson.data(), expectedStateJson.size()).c_str();
    }

}  // namespace BECore::Tests

/// Plays the replay file @p path into the `IGame` @p game and compares the state of the game with the JSON text @p expectedState; an optional fourth
/// argument is the `ReplayStepFn` of a game that has more to advance than the clock and the scene.
#define EXPECT_REPLAY_FINAL_STATE(game, path, expectedState, ...) ::BECore::Tests::ExpectReplayFinalState(game, path, expectedState __VA_OPT__(, ) __VA_ARGS__)
