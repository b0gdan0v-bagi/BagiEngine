#include "Timeline.h"

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

    Timeline& Timeline::AppendCallback(eastl::function<void()> callback) {
        return InsertCallback(_end, std::move(callback));
    }

    Timeline& Timeline::InsertCallback(float at, eastl::function<void()> callback) {
        Clip clip;
        clip.isCallback = true;
        clip.callback = std::move(callback);
        return AddClip(at, std::move(clip));
    }

    Timeline& Timeline::Repeat(int count) {
        _repeat = std::max(count, 1);
        return *this;
    }

    float Timeline::GetDuration() const {
        return _end * static_cast<float>(_repeat);
    }

    Timeline& Timeline::AddTween(float at, const TweenSettings& settings, eastl::function<void(float)> apply) {
        Clip clip;
        clip.delay = NonNegative(settings.delay);
        clip.length = NonNegative(settings.duration);
        clip.ease = settings.ease;
        clip.apply = std::move(apply);
        return AddClip(at, std::move(clip));
    }

    Timeline& Timeline::AddSub(float at, Timeline&& timeline) {
        Clip clip;
        clip.length = timeline.GetDuration();
        clip.sub = eastl::make_unique<Timeline>(std::move(timeline));
        return AddClip(at, std::move(clip));
    }

    Timeline& Timeline::AddClip(float at, Clip&& clip) {
        clip.start = NonNegative(at);
        _lastStart = clip.start;
        _end = std::max(_end, (clip.start + clip.delay) + clip.length);
        // After every clip that starts at the same time or earlier: equal starts keep the insertion order
        const auto position = eastl::upper_bound(_clips.begin(), _clips.end(), clip.start, [](float start, const Clip& other) { return start < other.start; });
        _clips.insert(position, std::move(clip));
        return *this;
    }

    void Timeline::Evaluate(float time) {
        if (_finished || std::isnan(time)) {
            return;
        }
        time = std::max(time, 0.0f);

        // No length: done by the first call, and nothing to loop over
        if (_end <= 0.0f) {
            VisitClips(0.0f);
            _finished = true;
            return;
        }

        // Decided once, from the duration the owner knows: the subtraction below can round a hair under `_end` in the last iteration,
        // and a call at `GetDuration()` must still finish every clip with its exact final value
        const bool finishing = time >= GetDuration();
        while (true) {
            const float local = time - static_cast<float>(_iteration) * _end;
            if (_iteration + 1 >= _repeat) {
                VisitClips(finishing ? _end : local);
                _finished = finishing || local >= _end;
                return;
            }
            if (!finishing && local < _end) {
                VisitClips(local);
                return;
            }
            // The iteration is over and another follows: finish it, start the next one and go on with the left-over time
            VisitClips(_end);
            RestartClips();
            ++_iteration;
        }
    }

    void Timeline::Reset() {
        _iteration = 0;
        _finished = false;
        RestartClips();
    }

    void Timeline::VisitClips(float time) {
        // The list is in order of start: past the first clip that has not started, none has
        for (size_t index = _cursor; index < _clips.size() && _clips[index].start <= time; ++index) {
            if (!_clips[index].done) {
                VisitClip(_clips[index], time);
            }
        }
        while (_cursor < _clips.size() && _clips[_cursor].done) {
            ++_cursor;
        }
    }

    void Timeline::VisitClip(Clip& clip, float time) {
        if (clip.isCallback) {
            clip.done = true;
            if (clip.callback) {
                clip.callback();
            }
            return;
        }

        const float activeStart = clip.start + clip.delay;
        const float end = activeStart + clip.length;
        if (clip.sub) {
            // Past the end the sub-timeline gets exactly its duration, so rounding of the subtraction cannot leave it unfinished
            clip.sub->Evaluate(time >= end ? clip.length : time - clip.start);
            clip.done = clip.sub->IsFinished();
            return;
        }

        if (time < activeStart) {
            return;
        }
        // A clip with no length, or one the time has passed, gets its final value exactly once
        clip.done = clip.length <= 0.0f || time >= end;
        const float progress = clip.done ? 1.0f : (time - activeStart) / clip.length;
        if (clip.apply) {
            clip.apply(Ease(clip.ease, progress));
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
