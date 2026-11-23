#include "ReplayPlayer.h"

#include <BECore/Control/Json.h>
#include <Games/IControllableGame.h>

namespace BECore {

    namespace {

        ReplayMismatch MakeMismatch(size_t index, const ReplayEntry& entry, ReplayMismatchKind kind) {
            ReplayMismatch mismatch;
            mismatch.index = index;
            mismatch.entry = entry;
            mismatch.entryText = FormatReplayEntry(entry);
            mismatch.kind = kind;
            return mismatch;
        }

        ReplayMismatch MakeRefused(size_t index, const ReplayEntry& entry, ControlError error) {
            ReplayMismatch mismatch = MakeMismatch(index, entry, ReplayMismatchKind::Refused);
            mismatch.error = std::move(error);
            return mismatch;
        }

        /// The text of an entry is JSON a tape accepted when the file was read; an entry made by hand that is not is refused like a call the game refused
        std::expected<Json, ReplayMismatch> ParseEntryText(size_t index, const ReplayEntry& entry, const String& text) {
            auto parsed = ParseJson(StringView(text.c_str(), text.size()));
            if (!parsed) {
                return std::unexpected(MakeRefused(index, entry, MakeControlError(ControlErrors::kInvalidParams, "the entry holds no JSON the DOM reads")));
            }
            return std::move(*parsed);
        }

        ReplayStep ApplyReset(IControllableGame& game, size_t index, const ReplayEntry& entry) {
            auto params = ParseEntryText(index, entry, entry.params);
            if (!params) {
                return std::unexpected(std::move(params.error()));
            }
            if (auto reset = game.Reset(*params); !reset) {
                return std::unexpected(MakeRefused(index, entry, std::move(reset.error())));
            }
            // A game that took another seed than the file asks for plays another game from here on
            Json start = game.GetReplayStart();
            if (start != *params) {
                ReplayMismatch mismatch = MakeMismatch(index, entry, ReplayMismatchKind::StartDiffers);
                mismatch.expected = entry.params;
                mismatch.actual = DumpJson(start);
                return std::unexpected(std::move(mismatch));
            }
            return {};
        }

        ReplayStep ApplyAction(IControllableGame& game, size_t index, const ReplayEntry& entry) {
            auto params = ParseEntryText(index, entry, entry.params);
            if (!params) {
                return std::unexpected(std::move(params.error()));
            }
            auto outcome = game.PerformAction(*params);
            if (!outcome) {
                return std::unexpected(MakeRefused(index, entry, std::move(outcome.error())));
            }
            // A file without an outcome (written by hand) checks only that the action is accepted
            if (entry.outcome) {
                auto expected = ParseEntryText(index, entry, *entry.outcome);
                if (!expected) {
                    return std::unexpected(std::move(expected.error()));
                }
                if (*expected != *outcome) {
                    ReplayMismatch mismatch = MakeMismatch(index, entry, ReplayMismatchKind::OutcomeDiffers);
                    mismatch.expected = *entry.outcome;
                    mismatch.actual = DumpJson(*outcome);
                    return std::unexpected(std::move(mismatch));
                }
            }
            return {};
        }

        ReplayStep ApplyState(IControllableGame& game, size_t index, const ReplayEntry& entry) {
            auto params = ParseEntryText(index, entry, entry.params);
            if (!params) {
                return std::unexpected(std::move(params.error()));
            }
            if (auto loaded = game.SetState(*params); !loaded) {
                return std::unexpected(MakeRefused(index, entry, std::move(loaded.error())));
            }
            return {};
        }

    }  // namespace

    StringView ReplayMismatchKindName(ReplayMismatchKind kind) {
        switch (kind) {
            case ReplayMismatchKind::Refused:
                return "refused";
            case ReplayMismatchKind::OutcomeDiffers:
                return "outcome_differs";
            case ReplayMismatchKind::StartDiffers:
                return "start_differs";
        }
        return {};
    }

    std::expected<void, ReplayError> ReplayPlayer::Load(StringView text, StringView gameName) {
        auto parsed = ParseReplay(text);
        if (!parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
        if (StringView(parsed->header.game) != gameName) {
            return std::unexpected(ReplayError{1, Format("the file is of the game '{}', the active game is '{}'", parsed->header.game, gameName)});
        }
        _replay = std::move(*parsed);
        _next = 0;
        _stopped = false;
        return {};
    }

    size_t ReplayPlayer::GetEntryCount() const {
        return _replay.entries.size();
    }

    bool ReplayPlayer::IsDone() const {
        return _stopped || _next >= _replay.entries.size();
    }

    ReplayGap ReplayPlayer::PeekGap() const {
        return IsDone() ? ReplayGap{} : _replay.entries[_next].gap;
    }

    ReplayStep ReplayPlayer::Apply(IControllableGame& game) {
        ASSERT(!IsDone());
        const size_t index = _next;
        const ReplayEntry& entry = _replay.entries[index];

        ReplayStep step;
        switch (entry.op) {
            case ReplayOp::Reset:
                step = ApplyReset(game, index, entry);
                break;
            case ReplayOp::Action:
                step = ApplyAction(game, index, entry);
                break;
            case ReplayOp::State:
                step = ApplyState(game, index, entry);
                break;
        }
        if (step) {
            ++_next;
        } else {
            _stopped = true;
        }
        return step;
    }

}  // namespace BECore
