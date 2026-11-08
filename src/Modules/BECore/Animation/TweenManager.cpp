#include "TweenManager.h"

#include <cmath>

namespace BECore {

    void TweenHandle::Cancel() {
        if (_manager != nullptr && !IsFinished()) {
            _manager->CancelTween(*this);
        }
    }

    TweenManager::~TweenManager() {
        Clear();
    }

    IntrusivePtr<TweenHandle> TweenManager::StartErased(const TweenSettings& settings, eastl::function<void(float)> apply, eastl::function<void()> onComplete, eastl::function<void()> onCancel) {
        auto tween = New<TweenHandle>();
        tween->_manager = this;
        tween->_settings = settings;
        // A NaN or infinite time would never finish the tween (NaN) or never reach its end: both count as zero
        if (!std::isfinite(tween->_settings.duration)) {
            tween->_settings.duration = 0.0f;
        }
        if (!std::isfinite(tween->_settings.delay)) {
            tween->_settings.delay = 0.0f;
        }
        tween->_apply = std::move(apply);
        tween->_onComplete = std::move(onComplete);
        tween->_onCancel = std::move(onCancel);
        (_walking ? _pending : _tweens).push_back(tween);
        return tween;
    }

    void TweenManager::ReleaseCallbacks(TweenHandle& tween) {
        if (&tween != _stepping) {
            tween._apply = nullptr;  // the setter that is running right now is released by Step, after it returns
        }
        tween._onComplete = nullptr;
        tween._onCancel = nullptr;
    }

    IntrusivePtr<TweenHandle> TweenManager::Delay(float seconds, eastl::function<void()> onComplete, eastl::function<void()> onCancel) {
        // No setter: the same path as any tween (the loop calls `_apply` always, so it is a function that does nothing)
        return StartErased({.duration = seconds}, [](float) {}, std::move(onComplete), std::move(onCancel));
    }

    TweenAwaiter TweenManager::Wait(float seconds) {
        return AwaitTween(Delay(seconds));
    }

    IntrusivePtr<TweenHandle> TweenManager::Play(Timeline timeline, float delay, eastl::function<void()> onComplete, eastl::function<void()> onCancel) {
        const float duration = timeline.GetDuration();
        // `eastl::function` needs a copyable callable and a Timeline is move-only: the tween owns it through a shared pointer
        auto owned = eastl::make_shared<Timeline>(std::move(timeline));
        // The progress of the last step is exactly 1, so the last call is exactly the duration; the tween itself is linear
        auto apply = [owned, duration](float progress) { owned->Evaluate(progress * duration); };
        auto handle = StartErased({.duration = duration, .delay = delay, .ease = EaseType::Linear}, std::move(apply), std::move(onComplete), std::move(onCancel));
        // A clip callback may cancel its own run (Cancel, CancelAll, a ScopedTween, Clear): the timeline then stops at the clip boundary, so no setter or
        // callback runs after it. The raw pointer is safe: the timeline belongs to the setter, which belongs to the handle and is released with it
        owned->SetStopCheck([tween = handle.Get()] { return tween->IsFinished(); });
        return handle;
    }

    void TweenManager::ResumeWaiters(TweenHandle& tween) {
        if (tween._waiters.empty()) {
            return;
        }
        // A waiter may drop the last reference to the handle (it holds one in its awaiter), so keep it alive for the loop
        const IntrusivePtr<TweenHandle> keepAlive(&tween);
        // One at a time, taken out of the list before it runs: a waiter that is destroyed by an earlier one unregisters itself and is not resumed.
        // A waiter that calls Clear() empties this list too (the tween is still in the lists of the manager), so the rest are dropped, not resumed
        while (!tween._waiters.empty()) {
            const std::coroutine_handle<> continuation = tween._waiters.front().continuation;
            tween._waiters.erase(tween._waiters.begin());
            continuation.resume();
        }
    }

    void TweenManager::CancelTween(TweenHandle& tween) {
        tween._state = TweenState::Cancelled;
        tween._manager = nullptr;
        // The callback is moved out first: it may start tweens or cancel others, and this tween is already finished
        auto onCancel = std::move(tween._onCancel);
        ReleaseCallbacks(tween);
        if (onCancel) {
            onCancel();
        }
        ResumeWaiters(tween);
    }

    void TweenManager::Step(TweenHandle& tween, float dt) {
        tween._elapsed += dt;
        const TweenSettings& settings = tween._settings;
        if (tween._elapsed < settings.delay) {
            return;
        }
        tween._state = TweenState::Running;

        const float running = tween._elapsed - settings.delay;
        const bool done = settings.duration <= 0.0f || running >= settings.duration;
        tween._progress = done ? 1.0f : running / settings.duration;
        _stepping = &tween;
        tween._apply(Ease(settings.ease, tween._progress));
        _stepping = nullptr;
        if (tween.IsFinished()) {
            ReleaseCallbacks(tween);  // the setter cancelled its own tween (or cleared the manager): it could not be released while it ran
            return;
        }
        if (!done) {
            return;
        }

        tween._state = TweenState::Completed;
        tween._manager = nullptr;
        auto onComplete = std::move(tween._onComplete);
        ReleaseCallbacks(tween);
        if (onComplete) {
            onComplete();
        }
        ResumeWaiters(tween);
    }

    void TweenManager::Advance(float dt) {
        if (!(dt > 0.0f) || _walking) {
            return;  // also NaN; a call from inside a callback would walk the list twice
        }
        _walking = true;
        // The list does not grow during the walk (a start goes to _pending), so the size is fixed and a reference stays valid
        const size_t count = _tweens.size();
        for (size_t i = 0; i < count; ++i) {
            const IntrusivePtr<TweenHandle> tween = _tweens[i];  // keeps it alive whatever a callback does
            if (!tween->IsFinished()) {
                Step(*tween, dt);
            }
        }
        EndWalk();
    }

    void TweenManager::CancelAllIn(const eastl::vector<IntrusivePtr<TweenHandle>>& list) {
        // By index and to the size at the start: a callback may start a tween, which goes to the end of _pending and never moves _tweens
        const size_t count = list.size();
        for (size_t i = 0; i < count; ++i) {
            const IntrusivePtr<TweenHandle> tween = list[i];
            tween->Cancel();
        }
    }

    void TweenManager::CancelAll() {
        if (_walking) {
            // Inside a callback of this manager: cancel what runs now, the walk that called us compacts
            CancelAllIn(_tweens);
            CancelAllIn(_pending);
            return;
        }
        _walking = true;
        CancelAllIn(_tweens);
        EndWalk();
    }

    void TweenManager::EndWalk() {
        _walking = false;
        const auto finished = [](const IntrusivePtr<TweenHandle>& tween) { return tween->IsFinished(); };
        _tweens.erase(eastl::remove_if(_tweens.begin(), _tweens.end(), finished), _tweens.end());
        for (auto& tween : _pending) {
            if (!tween->IsFinished()) {
                _tweens.push_back(std::move(tween));
            }
        }
        _pending.clear();
    }

    void TweenManager::Clear() {
        const auto drop = [this](const IntrusivePtr<TweenHandle>& tween) {
            if (!tween->IsFinished()) {
                tween->_state = TweenState::Cancelled;
            }
            tween->_manager = nullptr;
            ReleaseCallbacks(*tween);
            tween->_waiters.clear();  // never resumed: the coroutine may use what is gone already
        };
        for (const auto& tween : _tweens) {
            drop(tween);
        }
        for (const auto& tween : _pending) {
            drop(tween);
        }
        if (!_walking) {
            _tweens.clear();
            _pending.clear();
        }  // else the walk in progress holds indices into the list: EndWalk removes what is finished
    }

    size_t TweenManager::GetActiveCount() const {
        const auto active = [](const IntrusivePtr<TweenHandle>& tween) { return !tween->IsFinished(); };
        return static_cast<size_t>(eastl::count_if(_tweens.begin(), _tweens.end(), active) + eastl::count_if(_pending.begin(), _pending.end(), active));
    }

}  // namespace BECore
