#include "ReplayControl.h"

#include <BECore/Application/CommandLine.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlClock.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/File/File.h>
#include <BECore/GameManager/CoreManager.h>
#include <Games/GameControl.h>
#include <Games/GameManager.h>
#include <Games/IControllableGame.h>
#include <Games/ReplayAnswers.h>
#include <Games/ReplayPlayer.h>
#include <Games/SessionRecorder.h>

namespace BECore {

    namespace {

        /// A replay file is client data: the ones that follow a person's session are far below this
        constexpr size_t kMaxReplayFileBytes = 64ULL * 1024 * 1024;

        void HandleRecordStart(ControlCall& call) {
            const auto path = GetJsonString(call.Params(), "path");
            if (!path || path->empty()) {
                call.Fail(ControlErrors::kInvalidParams, "'path' has to be a non-empty string");
                return;
            }
            String title;
            if (FindJsonMember(call.Params(), "title") != nullptr) {
                const auto requested = GetJsonString(call.Params(), "title");
                if (!requested) {
                    call.Fail(ControlErrors::kInvalidParams, "'title' has to be a string");
                    return;
                }
                title = *requested;
            }

            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                call.Fail(GameControlErrors::kNoActiveGame, "no game is active");
                return;
            }
            if (auto started = SessionRecorder::GetInstance().Start(*path, *game, title); !started) {
                call.Fail(started.error());
                return;
            }

            ReplayRecordStartAnswer answer;
            answer.game = game->GetName();
            answer.path = TextView(StringView(*path));
            call.ReplyObject(answer);
        }

        void HandleRecordStop(ControlCall& call) {
            SessionRecorder& recorder = SessionRecorder::GetInstance();
            if (!recorder.IsRecording()) {
                call.Fail(ControlErrors::kInvalidParams, "no recording is running");
                return;
            }
            ReplayRecordStopAnswer answer;
            answer.entries = recorder.Stop();
            call.ReplyObject(answer);
        }

        /**
         * One `replay` that waits for frames. The continuation of the deferred call owns it (the handler has returned by then), so whatever ends the replay
         * (an answer, an error, a Shutdown() that drops the call) destroys it.
         */
        struct ReplayRunState {
            ReplayPlayer player;
            String game;                   ///< The game the file is of; the replay stops when the active one is another
            uint64_t frames = 0;           ///< Frames stepped so far
            uint64_t gapFramesLeft = 0;    ///< Frames of the gap before the next entry that are still to step
            double gapFrameSeconds = 0.0;  ///< Seconds of each of them
            bool gapTaken = false;         ///< The gap of the next entry has been read from the player
            size_t applied = 0;            ///< Entries applied so far
        };

        /// The answer of `replay`: the counts and the clock of `step`, and the `mismatch` when an entry differed
        void ReplyReplay(ControlCall& call, const ReplayRunState& state, const ReplayMismatch* mismatch) {
            ReplayAnswer answer;
            static_cast<ControlClock&>(answer) = ControlClock::Read();
            answer.ok = mismatch == nullptr;
            answer.entries = state.applied;
            answer.frames = state.frames;
            if (mismatch != nullptr) {
                FillReplayMismatchAnswer(answer.mismatch.emplace(), *mismatch);
            }
            call.ReplyObject(answer);
        }

        /// The active game when it is still the one the replay started on
        std::expected<IControllableGame*, ControlError> FindReplayGame(const ReplayRunState& state) {
            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                return std::unexpected(MakeControlError(GameControlErrors::kNoActiveGame, "no game is active"));
            }
            IControllableGame* control = game->AsControllable();
            if (control == nullptr || StringView(state.game) != game->GetName().ToStringView()) {
                return std::unexpected(MakeControlError(GameControlErrors::kNoActiveGame, Format("the game '{}' is no longer the active one", state.game)));
            }
            return control;
        }

        /// Steps the gap of the next entry (in chunks the control API allows), then applies the entry, until the file is played or an entry differs
        void AdvanceReplay(ControlService& service, ControlCall& call, UniquePtr<ReplayRunState> state) {
            for (;;) {
                if (!state->gapTaken && !state->player.IsDone()) {
                    const ReplayGap gap = state->player.PeekGap();
                    state->gapFramesLeft = gap.frames;
                    state->gapFrameSeconds = gap.frames > 0 ? gap.seconds / static_cast<double>(gap.frames) : 0.0;
                    state->gapTaken = true;
                }
                if (state->gapFramesLeft > 0) {
                    const auto chunk = static_cast<uint32_t>(std::min<uint64_t>(state->gapFramesLeft, kMaxStepFrames));
                    state->gapFramesLeft -= chunk;
                    state->frames += chunk;
                    const double seconds = state->gapFrameSeconds;
                    service.DeferSteppedFrames(call, chunk, seconds, [&service, state = std::move(state)](ControlCall& again) mutable { AdvanceReplay(service, again, std::move(state)); });
                    return;
                }
                if (state->player.IsDone()) {
                    ReplyReplay(call, *state, nullptr);
                    return;
                }

                const auto game = FindReplayGame(*state);
                if (!game) {
                    call.Fail(game.error());
                    return;
                }
                if (const ReplayStep step = state->player.Apply(**game); !step) {
                    ReplyReplay(call, *state, &step.error());
                    return;
                }
                ++state->applied;
                state->gapTaken = false;
            }
        }

        std::expected<String, ControlError> ReadReplayFile(StringView path) {
            // The size is checked before the read so the refusal names the limit; ReadTextFile checks it again against a file that grew in between
            const Optional<FileInfo> info = GetFileInfo(path);
            if (!info || info->kind != FileKind::File) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("cannot read '{}'", path)));
            }
            if (info->size > kMaxReplayFileBytes) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' is larger than {} bytes", path, kMaxReplayFileBytes)));
            }
            auto text = ReadTextFile(path, kMaxReplayFileBytes);
            if (!text) {
                return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("cannot read '{}'", path)));
            }
            return std::move(*text);
        }

        void HandleReplay(ControlCall& call, ControlService& service) {
            const auto path = GetJsonString(call.Params(), "path");
            if (!path || path->empty()) {
                call.Fail(ControlErrors::kInvalidParams, "'path' has to be a non-empty string");
                return;
            }
            if (SessionRecorder::GetInstance().IsRecording()) {
                call.Fail(ControlErrors::kInvalidParams, "a recording is running: stop it before a replay");
                return;
            }
            IGame* game = GameManager::GetInstance().GetActive();
            if (game == nullptr) {
                call.Fail(GameControlErrors::kNoActiveGame, "no game is active");
                return;
            }
            const IControllableGame* control = game->AsControllable();
            if (control == nullptr) {
                call.Fail(GameControlErrors::kNotControllable, Format("the game '{}' cannot be controlled from outside", game->GetName().ToStringView()));
                return;
            }
            if (!control->IsReplayable()) {
                call.Fail(GameControlErrors::kUnsupported, Format("the game '{}' is not replayable", game->GetName().ToStringView()));
                return;
            }
            if (!service.IsManualClock()) {
                call.Fail(GameControlErrors::kReplayNeedsManualClock, "a replay passes the game time of the file on the manual clock: start with --control --manual-clock");
                return;
            }

            const auto text = ReadReplayFile(*path);
            if (!text) {
                call.Fail(text.error());
                return;
            }
            auto state = MakeUnique<ReplayRunState>();
            state->game = String(game->GetName().ToStringView());
            if (const auto loaded = state->player.Load(*text, game->GetName().ToStringView()); !loaded) {
                call.Fail(ControlErrors::kInvalidParams, Format("'{}': {}", *path, DescribeReplayError(loaded.error())));
                return;
            }
            AdvanceReplay(service, call, std::move(state));
        }

    }  // namespace

    void RegisterReplayControl(ControlService& service) {
        service.RegisterHandler("record_start", HandleRecordStart);
        service.RegisterHandler("record_stop", HandleRecordStop);
        service.RegisterHandler("replay", [&service](ControlCall& call) { HandleReplay(call, service); });

        service.DescribeMethod("record_start", {"Records the accepted reset, perform_action and set_state calls of the active game into a replay file; the game has to be replayable",
                                                {{"path", "string", true, "The file to write on the host the game runs on; an existing file is replaced"},
                                                 {"title", "string", false, "Free text for the header of the file"}}});
        service.DescribeMethod("record_stop", {"Ends the recording; the answer is the number of entries the file holds", {}});
        service.DescribeMethod("replay", {"Plays a replay file into the active game on the manual clock; a mismatch is an answer with ok false that names the first entry that differs, not an error",
                                          {{"path", "string", true, "The file to read on the host the game runs on"}}});
    }

    void StartRecordingFromCommandLine() {
        const CommandLine& commandLine = CoreManager::GetCommandLine();
        const Optional<StringView> path = commandLine.FindValue(kRecordArgument);
        if (!path) {
            if (commandLine.HasFlag(kRecordArgument)) {
                LOG_ERROR("The flag '--record' needs a file: --record=FILE; the game starts without recording");
            }
            return;
        }
        if (path->empty()) {
            LOG_ERROR("The flag '--record=' has no file; the game starts without recording");
            return;
        }

        IGame* game = GameManager::GetInstance().GetActive();
        if (game == nullptr) {
            LOG_ERROR("The flag '--record' needs an active game; the game starts without recording");
            return;
        }
        if (const auto started = SessionRecorder::GetInstance().Start(*path, *game); !started) {
            LOG_ERROR("Cannot record to '{}': {}; the game starts without recording", *path, started.error().message);
            return;
        }
        LOG_INFO("Recording the game '{}' to '{}'", game->GetName().ToStringView(), *path);
    }

}  // namespace BECore
