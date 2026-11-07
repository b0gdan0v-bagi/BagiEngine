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

    void TweenManager::ResumeWaiters(TweenHandle& tween) {
        if (tween._waiters.empty()) {
            return;
        }
        // A waiter may drop the last reference to the handle (it holds one in its awaiter), so keep it alive for the loop
        const IntrusivePtr<TweenHandle> keepAlive(&tween);
        const size_t clearsAtStart = _clearCount;
        // One at a time, taken out of the list before it runs: a waiter that is destroyed by an earlier one unregisters itself and is not resumed
        while (!tween._waiters.empty()) {
            if (_clearCount != clearsAtStart) {
                tween._waiters.clear();  // a waiter called Clear(): the rest are dropped, not resumed
                return;
            }
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

    void TweenManager::CancelFirst(const eastl::vector<IntrusivePtr<TweenHandle>>& list, size_t count) {
        // By index and to a fixed size: a callback may start a tween, which goes to the end of _pending and never moves _tweens
        for (size_t i = 0; i < count; ++i) {
            const IntrusivePtr<TweenHandle> tween = list[i];
            tween->Cancel();
        }
    }

    void TweenManager::CancelAll() {
        if (_walking) {
            // Inside a callback of this manager: cancel what runs now, the walk that called us compacts
            CancelFirst(_tweens, _tweens.size());
            CancelFirst(_pending, _pending.size());
            return;
        }
        _walking = true;
        CancelFirst(_tweens, _tweens.size());
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
        ++_clearCount;
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
