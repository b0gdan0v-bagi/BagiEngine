#include "TweenManager.h"

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

    void TweenManager::CancelTween(TweenHandle& tween) {
        tween._state = TweenState::Cancelled;
        tween._manager = nullptr;
        // The callback is moved out first: it may start tweens or cancel others, and this tween is already finished
        auto onCancel = std::move(tween._onCancel);
        ReleaseCallbacks(tween);
        if (onCancel) {
            onCancel();
        }
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

    void TweenManager::CancelAll() {
        if (_walking) {
            // Inside a callback of this manager: cancel what runs now, the walk that called us compacts
            // By index and to a fixed size: a callback may start a tween, which goes to the end of _pending
            const size_t tweenCount = _tweens.size();
            for (size_t i = 0; i < tweenCount; ++i) {
                const IntrusivePtr<TweenHandle> tween = _tweens[i];
                tween->Cancel();
            }
            const size_t pendingCount = _pending.size();
            for (size_t i = 0; i < pendingCount; ++i) {
                const IntrusivePtr<TweenHandle> tween = _pending[i];
                tween->Cancel();
            }
            return;
        }
        _walking = true;
        const size_t count = _tweens.size();
        for (size_t i = 0; i < count; ++i) {
            const IntrusivePtr<TweenHandle> tween = _tweens[i];
            tween->Cancel();
        }
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
