#include "ReplayPlayer.h"

#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
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

        /// The text of an entry is JSON a tape accepted when the file was read; one made by hand that is not is refused like a call the game refused
        std::expected<JsonView, ReplayMismatch> TapeEntryText(size_t index, const ReplayEntry& entry, const String& text, JsonTape& tape) {
            if (const auto parsed = tape.Parse(StringView(text)); !parsed) {
                return std::unexpected(MakeRefused(index, entry, MakeControlError(ControlErrors::kInvalidParams, Format("the entry holds no JSON: {}", parsed.error().message))));
            }
            return JsonView(tape, 0);
        }

        /// Runs @p write on a writer over @p text (cleared first) and tapes what it wrote; the error is what the game answered, or `internal_error` for an incomplete value
        template <typename TWrite>
        std::expected<JsonView, ControlError> WriteAndTape(String& text, JsonTape& tape, TWrite&& write) {
            text.clear();
            JsonWriter writer(text);
            if (auto written = write(writer); !written) {
                return std::unexpected(std::move(written.error()));
            }
            if (!writer.IsComplete()) {
                return std::unexpected(MakeControlError(ControlErrors::kInternalError, "the game answered with no complete value"));
            }
            if (const auto parsed = tape.Parse(StringView(text)); !parsed) {
                return std::unexpected(MakeControlError(ControlErrors::kInternalError, Format("the game wrote no readable value: {}", parsed.error().message)));
            }
            return JsonView(tape, 0);
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

    ReplayStep ReplayPlayer::ApplyReset(IControllableGame& game, size_t index, const ReplayEntry& entry) {
        const auto params = TapeEntryText(index, entry, entry.params, _paramsTape);
        if (!params) {
            return std::unexpected(params.error());
        }
        if (auto reset = game.Reset(*params); !reset) {
            return std::unexpected(MakeRefused(index, entry, std::move(reset.error())));
        }
        // A game that took another seed than the file asks for plays another game from here on
        const auto start = WriteAndTape(_actualText, _actualTape, [&](JsonWriter& writer) { return game.WriteReplayStart(writer); });
        if (!start) {
            return std::unexpected(MakeRefused(index, entry, start.error()));
        }
        if (!JsonEquals(*start, *params)) {
            ReplayMismatch mismatch = MakeMismatch(index, entry, ReplayMismatchKind::StartDiffers);
            mismatch.expected = entry.params;
            mismatch.actual = _actualText;
            return std::unexpected(std::move(mismatch));
        }
        return {};
    }

    ReplayStep ReplayPlayer::ApplyAction(IControllableGame& game, size_t index, const ReplayEntry& entry) {
        const auto params = TapeEntryText(index, entry, entry.params, _paramsTape);
        if (!params) {
            return std::unexpected(params.error());
        }
        const auto outcome = WriteAndTape(_actualText, _actualTape, [&](JsonWriter& writer) { return game.PerformAction(*params, writer); });
        if (!outcome) {
            return std::unexpected(MakeRefused(index, entry, outcome.error()));
        }
        // A file without an outcome (written by hand) checks only that the action is accepted
        if (entry.outcome) {
            const auto expected = TapeEntryText(index, entry, *entry.outcome, _expectedTape);
            if (!expected) {
                return std::unexpected(expected.error());
            }
            if (!JsonEquals(*expected, *outcome)) {
                ReplayMismatch mismatch = MakeMismatch(index, entry, ReplayMismatchKind::OutcomeDiffers);
                mismatch.expected = *entry.outcome;
                mismatch.actual = _actualText;
                return std::unexpected(std::move(mismatch));
            }
        }
        return {};
    }

    ReplayStep ReplayPlayer::ApplyState(IControllableGame& game, size_t index, const ReplayEntry& entry) {
        const auto params = TapeEntryText(index, entry, entry.params, _paramsTape);
        if (!params) {
            return std::unexpected(params.error());
        }
        if (auto loaded = game.SetState(*params); !loaded) {
            return std::unexpected(MakeRefused(index, entry, std::move(loaded.error())));
        }
        return {};
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
