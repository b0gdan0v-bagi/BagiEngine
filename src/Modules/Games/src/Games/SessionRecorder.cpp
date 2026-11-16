#include "SessionRecorder.h"

#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <BECore/Utils/EngineVersion.h>
#include <Games/IControllableGame.h>
#include <Games/IGame.h>

namespace BECore {

    std::expected<void, ControlError> SessionRecorder::Start(StringView path, IGame& game, StringView title) {
        if (_recording) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "a recording is already running"));
        }
        const IControllableGame* control = game.AsControllable();
        if (control == nullptr) {
            return std::unexpected(MakeControlError(GameControlErrors::kNotControllable, Format("the game '{}' cannot be controlled from outside", game.GetName().ToStringView())));
        }
        if (!control->IsReplayable()) {
            return std::unexpected(MakeControlError(GameControlErrors::kUnsupported, Format("the game '{}' is not replayable", game.GetName().ToStringView())));
        }
        if (path.empty()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, "'path' is empty"));
        }

        _file.open(ToStd(path), std::ios::binary | std::ios::trunc);
        if (!_file) {
            _file.clear();
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("cannot write '{}'", path)));
        }

        _recording = true;
        _started = false;
        _warnedDropped = false;
        _entryCount = 0;
        const ReplayHeader header{String(game.GetName().ToStringView()), String(kEngineVersion), String(title)};
        _file << ToStd(FormatReplayHeader(header)) << '\n';
        _file.flush();
        if (!_file) {
            Fail("the header");
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("cannot write '{}'", path)));
        }
        return {};
    }

    size_t SessionRecorder::Stop() {
        if (_recording) {
            _recording = false;
            _file.close();
            _file.clear();
        }
        return _entryCount;
    }

    bool SessionRecorder::IsRecording() const {
        return _recording;
    }

    size_t SessionRecorder::GetEntryCount() const {
        return _entryCount;
    }

    void SessionRecorder::ReportReset(const Json& startParams) {
        Write(ReplayOp::Reset, startParams, nullptr);
    }

    void SessionRecorder::ReportAction(const Json& action, const Json& outcome) {
        Write(ReplayOp::Action, action, &outcome);
    }

    void SessionRecorder::ReportSetState(const Json& state) {
        Write(ReplayOp::State, state, nullptr);
    }

    void SessionRecorder::Write(ReplayOp op, const Json& params, const Json* outcome) {
        if (!_recording) {
            return;
        }
        if (op != ReplayOp::Reset && !_started) {
            if (!_warnedDropped) {
                _warnedDropped = true;
                LOG_WARNING("SessionRecorder: a {} before the first reset is not recorded: the file would not hold the state it acts on"_format(ReplayOpName(op)));
            }
            return;
        }

        const Time& time = CoreManager::GetTime();
        const uint64_t frame = time.GetFrameCount();
        const double seconds = time.GetTotalTime();
        ReplayEntry entry;
        entry.op = op;
        entry.params = params.is_object() ? params : Json::object();
        if (outcome != nullptr) {
            entry.outcome = *outcome;
        }
        // A clock that went back (a Time::Reset) is a gap of nothing, not a negative one
        if (_started && frame >= _lastFrame && seconds >= _lastSeconds) {
            entry.gap = ReplayGap{frame - _lastFrame, frame == _lastFrame ? 0.0 : seconds - _lastSeconds};
        }

        _file << ToStd(FormatReplayEntry(entry)) << '\n';
        _file.flush();
        if (!_file) {
            Fail("an entry");
            return;
        }
        _started = true;
        _lastFrame = frame;
        _lastSeconds = seconds;
        ++_entryCount;
    }

    void SessionRecorder::Fail(StringView what) {
        LOG_ERROR("SessionRecorder: cannot write {} to the replay file, the recording stops"_format(what));
        _recording = false;
        _file.close();
        _file.clear();
    }

}  // namespace BECore
