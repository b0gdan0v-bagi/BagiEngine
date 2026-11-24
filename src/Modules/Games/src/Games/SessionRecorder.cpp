#include "SessionRecorder.h"

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

        auto file = File::Open(path, FileMode::Write);
        if (!file) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("cannot write '{}'", path)));
        }

        _file = std::move(*file);
        _recording = true;
        _started = false;
        _warnedDropped = false;
        _entryCount = 0;
        const ReplayHeader header{String(game.GetName().ToStringView()), String(kEngineVersion), String(title)};
        _line = FormatReplayHeader(header);
        _line.push_back('\n');
        if (!WriteLineAndFlush()) {
            Fail("the header");
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("cannot write '{}'", path)));
        }
        return {};
    }

    size_t SessionRecorder::Stop() {
        if (_recording) {
            _recording = false;
            Close();
        }
        return _entryCount;
    }

    bool SessionRecorder::IsRecording() const {
        return _recording;
    }

    size_t SessionRecorder::GetEntryCount() const {
        return _entryCount;
    }

    void SessionRecorder::ReportReset(StringView startParamsText) {
        if (!_recording) {
            return;
        }
        // A text that does not tape is recorded as `{}`: the file keeps its shape and the replay then reports a start that differs
        const bool parsed = _startTape.Parse(startParamsText).has_value();
        Write(ReplayOp::Reset, parsed ? JsonView(_startTape, 0) : JsonView(), std::nullopt);
    }

    void SessionRecorder::ReportAction(JsonView action, StringView outcomeText) {
        Write(ReplayOp::Action, action, outcomeText);
    }

    void SessionRecorder::ReportSetState(JsonView state) {
        Write(ReplayOp::State, state, std::nullopt);
    }

    void SessionRecorder::Write(ReplayOp op, JsonView params, Optional<StringView> outcomeText) {
        if (!_recording) {
            return;
        }
        if (op != ReplayOp::Reset && !_started) {
            if (!_warnedDropped) {
                _warnedDropped = true;
                LOG_WARNING("SessionRecorder: a {} before the first reset is not recorded: the file would not hold the state it acts on", ReplayOpName(op));
            }
            return;
        }

        const Time& time = CoreManager::GetTime();
        const uint64_t frame = time.GetFrameCount();
        const double seconds = time.GetTotalTime();
        ReplayGap gap;
        // A clock that went back (a Time::Reset) is a gap of nothing, not a negative one
        if (_started && frame >= _lastFrame && seconds >= _lastSeconds) {
            gap = ReplayGap{frame - _lastFrame, frame == _lastFrame ? 0.0 : seconds - _lastSeconds};
        }

        _line.clear();
        AppendReplayEntry(_line, op, params, outcomeText, gap);
        _line.push_back('\n');
        if (!WriteLineAndFlush()) {
            Fail("an entry");
            return;
        }
        _started = true;
        _lastFrame = frame;
        _lastSeconds = seconds;
        ++_entryCount;
    }

    void SessionRecorder::Fail(StringView what) {
        LOG_ERROR("SessionRecorder: cannot write {} to the replay file, the recording stops", what);
        _recording = false;
        Close();
    }

    bool SessionRecorder::WriteLineAndFlush() {
        return _file.Write(_line) && _file.Flush();
    }

    void SessionRecorder::Close() {
        // Nothing more can be done about a failed close: the entries were flushed one by one
        static_cast<void>(_file.Close());
    }

}  // namespace BECore
