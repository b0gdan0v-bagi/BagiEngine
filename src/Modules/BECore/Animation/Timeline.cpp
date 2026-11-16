#include "Timeline.h"

#include <EASTL/sort.h>
#include <cmath>

namespace BECore {

    namespace {

        // A time that is negative or not finite counts as zero (a NaN would never finish, an infinity never be reached)
        float NonNegative(float seconds) {
            return std::isfinite(seconds) && seconds > 0.0f ? seconds : 0.0f;
        }

    }  // namespace

    Timeline& Timeline::Append(Timeline timeline) {
        return AddSub(_end, std::move(timeline));
    }

    Timeline& Timeline::Join(Timeline timeline) {
        return AddSub(_lastStart, std::move(timeline));
    }

    Timeline& Timeline::Insert(float at, Timeline timeline) {
        return AddSub(at, std::move(timeline));
    }

    Timeline& Timeline::AppendDelay(float seconds) {
        // A spacer calls nothing, so it is no clip: it only moves the end, and a `Join` after it starts where it starts
        _lastStart = _end;
        _end += NonNegative(seconds);
        return *this;
    }

    Timeline& Timeline::AppendCallback(MoveOnlyFunction<void()> callback) {
        return InsertCallback(_end, std::move(callback));
    }

    Timeline& Timeline::InsertCallback(float at, MoveOnlyFunction<void()> callback) {
        Clip clip;
        clip.isCallback = true;
        clip.callback = std::move(callback);
        return AddClip(at, std::move(clip));
    }

    Timeline& Timeline::Repeat(int count) {
        _repeat = std::max(count, 1);
        _endless = false;
        return *this;
    }

    float Timeline::GetDuration() const {
        if (_endless) {
            return _end > 0.0f ? std::numeric_limits<float>::infinity() : 0.0f;
        }
        return _end * static_cast<float>(_repeat);
    }

    Timeline& Timeline::AddTween(float at, const TweenSettings& settings, TweenApply apply) {
        Clip clip;
        clip.delay = NonNegative(settings.delay);
        clip.length = NonNegative(settings.duration);
        clip.ease = settings.ease;
        clip.apply = std::move(apply);
        return AddClip(at, std::move(clip));
    }

    Timeline& Timeline::AddSub(float at, Timeline&& timeline) {
        // A parent holds a finite length: an endless sub-timeline plays one iteration
        if (timeline._endless) {
            timeline._endless = false;
            timeline._repeat = 1;
        }
        Clip clip;
        clip.length = timeline.GetDuration();
        clip.sub = MakeUnique<Timeline>(std::move(timeline));
        return AddClip(at, std::move(clip));
    }

    Timeline& Timeline::AddClip(float at, Clip&& clip) {
        clip.start = NonNegative(at);
        clip.order = _inserted++;
        _backOrderBuilt = false;
        _lastStart = clip.start;
        _end = std::max(_end, (clip.start + clip.delay) + clip.length);
        // After every clip that starts at the same time or earlier: equal starts keep the insertion order
        const auto position = eastl::upper_bound(_clips.begin(), _clips.end(), clip.start, [](float start, const Clip& other) { return start < other.start; });
        _clips.insert(position, std::move(clip));
        return *this;
    }

    void Timeline::Evaluate(float time) {
        EvaluateWith(time, _shouldStop, false);
    }

    void Timeline::EvaluateWith(float time, const MoveOnlyFunction<bool()>& stop, bool reversed) {
        if (_finished || std::isnan(time) || (_endless && !std::isfinite(time)) || IsStopped(stop)) {
            return;
        }
        _reversed = reversed;
        time = std::max(time, 0.0f);

        if (_end <= 0.0f) {
            VisitClips(0.0f, stop);
            _finished = !IsStopped(stop);
            return;
        }

        // Decided once, from the duration the owner knows: the subtraction in `Run` can round a hair under `_end` in the last iteration,
        // and a call at `GetDuration()` must still finish every clip with its exact final value
        const bool finishing = time >= GetDuration();
        Run(static_cast<double>(time) - static_cast<double>(_iteration) * static_cast<double>(_end), finishing, stop);
    }

    void Timeline::Advance(float dt) {
        if (_finished || IsStopped(_shouldStop)) {
            return;
        }
        _reversed = false;

        if (_end <= 0.0f) {
            VisitClips(0.0f, _shouldStop);
            _finished = !IsStopped(_shouldStop);
            return;
        }
        Run(_clock + static_cast<double>(NonNegative(dt)), false, _shouldStop);
    }

    void Timeline::Run(double local, bool finishing, const MoveOnlyFunction<bool()>& stop) {
        const double length = static_cast<double>(_end);
        while (true) {
            if (!_endless && _iteration + 1 >= _repeat) {
                const bool reachesEnd = finishing || local >= length;
                _clock = local;
                VisitClips(reachesEnd ? _end : static_cast<float>(local), stop);
                _finished = !IsStopped(stop) && reachesEnd;
                return;
            }
            if (!finishing && local < length) {
                _clock = local;
                VisitClips(static_cast<float>(local), stop);
                return;
            }
            VisitClips(_end, stop);
            if (IsStopped(stop)) {
                return;
            }
            RestartClips();
            ++_iteration;
            local -= length;
        }
    }

    void Timeline::Reset() {
        _clock = 0.0;
        _iteration = 0;
        _finished = false;
        RestartClips();
    }

    bool Timeline::IsBackward() const {
        if (_end <= 0.0f) {
            return _reversed;  // one call, no iterations to alternate
        }
        // A reversed pass plays the forward iteration `_repeat - 1 - _iteration` backwards, so its direction is flipped
        const int source = _reversed ? _repeat - 1 - _iteration : _iteration;
        return (_yoyo && (source & 1) != 0) != _reversed;
    }

    void Timeline::BuildBackwardOrder() {
        _backOrder.clear();
        for (size_t index = 0; index < _clips.size(); ++index) {
            _backOrder.push_back(index);
        }
        // The mirror of the forward order: the latest end is entered first, equal ends in reverse insertion order
        eastl::sort(_backOrder.begin(), _backOrder.end(), [this](size_t left, size_t right) {
            const float leftEnter = EnterTime(_clips[left], true);
            const float rightEnter = EnterTime(_clips[right], true);
            return leftEnter != rightEnter ? leftEnter < rightEnter : _clips[left].order > _clips[right].order;
        });
        _backOrderBuilt = true;
    }

    void Timeline::VisitClips(float time, const MoveOnlyFunction<bool()>& stop) {
        const bool backward = IsBackward();
        if (backward && !_backOrderBuilt) {
            BuildBackwardOrder();
        }
        const auto clipAt = [this, backward](size_t position) -> Clip& { return _clips[backward ? _backOrder[position] : position]; };

        // The clips are in order of entering: past the first clip that has not been entered, none has
        for (size_t position = _cursor; position < _clips.size() && EnterTime(clipAt(position), backward) <= time; ++position) {
            Clip& clip = clipAt(position);
            if (!clip.done) {
                VisitClip(clip, time, stop, backward);
                if (IsStopped(stop)) {
                    break;  // a clip cancelled the run: nothing after it is called
                }
            }
        }
        while (_cursor < _clips.size() && clipAt(_cursor).done) {
            ++_cursor;
        }
    }

    void Timeline::VisitClip(Clip& clip, float time, const MoveOnlyFunction<bool()>& stop, bool backward) {
        if (clip.isCallback) {
            clip.done = true;
            if (clip.callback) {
                clip.callback();
            }
            return;
        }

        // The active part of the slot in this direction: the delay is before it forward and after it backward
        const float activeStart = clip.start + clip.delay;
        const float end = EndOf(clip);
        const float enter = backward ? _end - end : activeStart;
        const float leave = backward ? _end - activeStart : end;
        if (clip.sub) {
            // Past the end the sub-timeline gets exactly its duration, so rounding of the subtraction cannot leave it unfinished.
            // The sub-timeline plays reversed as a whole in a backward iteration
            clip.sub->EvaluateWith(time >= leave ? clip.length : time - enter, stop, backward);
            clip.done = clip.sub->IsFinished();
            return;
        }

        if (time < enter) {
            return;
        }
        // A clip with no length, or one the time has passed, gets its final value exactly once
        clip.done = clip.length <= 0.0f || time >= leave;
        const float progress = clip.done ? 1.0f : (time - enter) / clip.length;
        if (clip.apply) {
            // Backward the same curve is walked back, so the final call is `Ease(type, 0)`: exactly `from`
            clip.apply(Ease(clip.ease, backward ? 1.0f - progress : progress));
        }
    }

    void Timeline::RestartClips() {
        _cursor = 0;
        for (Clip& clip : _clips) {
            clip.done = false;
            if (clip.sub) {
                clip.sub->Reset();
            }
        }
    }

}  // namespace BECore
