#include "TweenManager.h"

#include <cmath>

namespace BECore {

    TweenState TweenHandle::GetState() const {
        return _manager != nullptr ? _manager->SlotOf(_id).state : _state;
    }

    float TweenHandle::GetProgress() const {
        return _manager != nullptr ? _manager->SlotOf(_id).progress : _progress;
    }

    void TweenHandle::Cancel() {
        if (_manager != nullptr) {
            _manager->CancelTween(_id);
        }
    }

    TweenManager::~TweenManager() {
        Clear();
    }

    TweenManager::Slot& TweenManager::SlotOf(uint32_t id) {
        const uint32_t index = _indexOfId[id];
        return (index & kPendingBit) != 0 ? _pending[index & ~kPendingBit] : _slots[index];
    }

    const TweenManager::Slot& TweenManager::SlotOf(uint32_t id) const {
        const uint32_t index = _indexOfId[id];
        return (index & kPendingBit) != 0 ? _pending[index & ~kPendingBit] : _slots[index];
    }

    uint32_t TweenManager::AcquireId() {
        if (!_freeIds.empty()) {
            const uint32_t id = _freeIds.back();
            _freeIds.pop_back();
            return id;
        }
        const auto id = static_cast<uint32_t>(_owners.size());
        _owners.emplace_back();
        _indexOfId.push_back(0);
        return id;
    }

    IntrusivePtr<TweenHandle> TweenManager::StartErased(const TweenSettings& settings, TweenApply apply, MoveOnlyFunction<void()> onComplete, MoveOnlyFunction<void()> onCancel, TweenRunMode mode,
                                                        AnimationFrameClass frameClass) {
        auto tween = New<TweenHandle>();
        const uint32_t id = AcquireId();
        tween->_manager = this;
        tween->_id = id;
        tween->_frameClass = frameClass;
        tween->_onComplete = std::move(onComplete);
        tween->_onCancel = std::move(onCancel);
        // A NaN or infinite time would never finish the tween (NaN) or never reach its end: both count as zero
        Slot slot{.duration = std::isfinite(settings.duration) ? settings.duration : 0.0f,
                  .delay = std::isfinite(settings.delay) ? settings.delay : 0.0f,
                  .id = id,
                  .ease = settings.ease,
                  .frameClass = frameClass,
                  .mode = mode,
                  .apply = std::move(apply)};
        _owners[id] = tween;
        if (_walking) {
            _indexOfId[id] = static_cast<uint32_t>(_pending.size()) | kPendingBit;
            _pending.push_back(std::move(slot));
        } else {
            _indexOfId[id] = static_cast<uint32_t>(_slots.size());
            _slots.push_back(std::move(slot));
        }
        return tween;
    }

    IntrusivePtr<TweenHandle> TweenManager::LoopErased(const TweenSettings& settings, TweenApply apply, MoveOnlyFunction<void()> onCancel) {
        // A turn of no length has no time to wrap into
        const bool loops = std::isfinite(settings.duration) && settings.duration > 0.0f;
        return StartErased(settings, std::move(apply), {}, std::move(onCancel), loops ? TweenRunMode::Loop : TweenRunMode::Once,
                           loops ? AnimationFrameClass::DecorativeLoop : AnimationFrameClass::FiniteEffect);
    }

    IntrusivePtr<TweenHandle> TweenManager::Delay(float seconds, MoveOnlyFunction<void()> onComplete, MoveOnlyFunction<void()> onCancel) {
        // `Step` calls the setter unconditionally, so the no-setter tween gets a function that does nothing
        return StartErased({.duration = seconds}, [](float) {}, std::move(onComplete), std::move(onCancel));
    }

    TweenAwaiter TweenManager::Wait(float seconds) {
        return AwaitTween(Delay(seconds));
    }

    IntrusivePtr<TweenHandle> TweenManager::Play(Timeline timeline, float delay, MoveOnlyFunction<void()> onComplete, MoveOnlyFunction<void()> onCancel) {
        const bool endless = timeline.IsEndless();
        const AnimationFrameClass frameClass = timeline.GetFrameClass();
        // An endless duration is infinite, which `StartErased` would turn into zero: the endless handle never looks at its duration
        const float duration = endless ? 0.0f : timeline.GetDuration();
        // The setter owns the timeline: one allocation, no control block
        auto owned = MakeUnique<Timeline>(std::move(timeline));
        Timeline* const running = owned.get();
        // The progress of the last step is exactly 1, so the last call is exactly the duration; the tween itself is linear. An endless run gets
        // the seconds of the step instead of a progress, so no ever larger time is accumulated into one float
        TweenApply apply = endless ? TweenApply([owned = std::move(owned)](float seconds) { owned->Advance(seconds); })
                                   : TweenApply([owned = std::move(owned), duration](float progress) { owned->Evaluate(progress * duration); });
        auto handle = StartErased({.duration = duration, .delay = delay, .ease = EaseType::Linear}, std::move(apply), std::move(onComplete), std::move(onCancel),
                                  endless ? TweenRunMode::Endless : TweenRunMode::Once, frameClass);
        // A clip callback may cancel its own run (Cancel, CancelAll, a ScopedTween, Clear): the timeline then stops at the clip boundary, so no setter or
        // callback runs after it. The raw pointer is safe: the timeline belongs to the setter, which belongs to the slot, and the handle outlives a setter
        // that runs (`_steppingOwner`)
        running->SetStopCheck([tween = handle.Get()] { return tween->IsFinished(); });
        return handle;
    }

    void TweenManager::ResumeWaiters(TweenHandle& tween, uint32_t clearCount) {
        if (tween._waiters.empty()) {
            return;
        }
        // A waiter may drop the last reference to the handle (it holds one in its awaiter), so keep it alive for the loop
        const IntrusivePtr<TweenHandle> keepAlive(&tween);
        // One at a time, taken out of the list before it runs: a waiter that is destroyed by an earlier one unregisters itself and is not resumed.
        // A `Clear()` since the finish (a callback or an earlier waiter called it) drops the rest: the tween is out of the lists by then, so
        // `Clear` cannot reach its waiters itself
        while (!tween._waiters.empty()) {
            if (clearCount != _clearCount) {
                tween._waiters.clear();
                return;
            }
            const std::coroutine_handle<> continuation = tween._waiters.front().continuation;
            tween._waiters.erase(tween._waiters.begin());
            continuation.resume();
        }
    }

    TweenManager::Retired TweenManager::Retire(Slot& slot, TweenState terminal) {
        const uint32_t id = slot.id;
        Retired retired{.owner = std::move(_owners[id])};
        TweenHandle& handle = *retired.owner;
        // The state first, the callables after: their destructors may reach this tween or the manager again and must see it finished
        handle._state = terminal;
        handle._progress = slot.progress;
        handle._manager = nullptr;
        slot.state = terminal;
        if ((_indexOfId[id] & kPendingBit) == 0) {
            ++_tombstones;
        }
        if (id == _stepping) {
            // Its setter is running: it is destroyed (and the handle released) by `CallSetter` after it returns
            _steppingOwner = retired.owner;
        } else {
            retired.apply = std::move(slot.apply);
        }
        return retired;
    }

    void TweenManager::CancelTween(uint32_t id) {
        Retired retired = Retire(SlotOf(id), TweenState::Cancelled);
        const uint32_t clearCount = _clearCount;
        TweenHandle& handle = *retired.owner;
        // The callback is moved out first: it may start tweens or cancel others, and this tween is already finished
        auto onCancel = std::move(handle._onCancel);
        handle._onComplete = nullptr;
        retired.apply = nullptr;
        if (onCancel) {
            onCancel();
        }
        ResumeWaiters(handle, clearCount);
    }

    void TweenManager::Complete(Slot& slot) {
        Retired retired = Retire(slot, TweenState::Completed);
        const uint32_t clearCount = _clearCount;
        TweenHandle& handle = *retired.owner;
        auto onComplete = std::move(handle._onComplete);
        handle._onCancel = nullptr;
        retired.apply = nullptr;
        if (onComplete) {
            onComplete();
        }
        ResumeWaiters(handle, clearCount);
    }

    void TweenManager::CallSetter(Slot& slot, float value) {
        _stepping = slot.id;
        slot.apply(value);
        _stepping = kNoId;
        if (!IsLive(slot.state)) {
            // The setter cancelled its own tween (or cleared the manager): `Retire` could not release it while it ran. The handle goes last
            const IntrusivePtr<TweenHandle> owner = std::move(_steppingOwner);
            const TweenApply released = std::move(slot.apply);
        }
    }

    void TweenManager::StepEndless(Slot& slot, float dt) {
        float seconds = dt;
        if (slot.state == TweenState::Waiting) {
            slot.elapsed += dt;
            if (slot.elapsed < slot.delay) {
                return;
            }
            slot.state = TweenState::Running;
            seconds = slot.elapsed - slot.delay;  // rule 3: the time left after the delay
            slot.elapsed = slot.delay;            // nothing grows past it: the timeline keeps the time
        }
        CallSetter(slot, seconds);
    }

    void TweenManager::StepLoop(Slot& slot, float dt) {
        slot.elapsed += dt;
        if (slot.state == TweenState::Waiting) {
            if (slot.elapsed < slot.delay) {
                return;
            }
            slot.state = TweenState::Running;
            slot.elapsed -= slot.delay;  // once: the next turns have no delay
        }
        if (slot.elapsed >= slot.duration) {
            // An infinite step has no place in a turn (`fmod` would answer NaN): the next turn starts over
            slot.elapsed = std::isfinite(slot.elapsed) ? std::fmod(slot.elapsed, slot.duration) : 0.0f;
        }
        slot.progress = slot.elapsed / slot.duration;
        CallSetter(slot, Ease(slot.ease, slot.progress));
    }

    void TweenManager::Step(Slot& slot, float dt) {
        if (slot.mode == TweenRunMode::Loop) {
            StepLoop(slot, dt);
            return;
        }
        if (slot.mode == TweenRunMode::Endless) {
            StepEndless(slot, dt);
            return;
        }
        slot.elapsed += dt;
        if (slot.elapsed < slot.delay) {
            return;
        }
        slot.state = TweenState::Running;

        const float running = slot.elapsed - slot.delay;
        const bool done = slot.duration <= 0.0f || running >= slot.duration;
        slot.progress = done ? 1.0f : running / slot.duration;
        CallSetter(slot, Ease(slot.ease, slot.progress));
        if (done && IsLive(slot.state)) {
            Complete(slot);
        }
    }

    void TweenManager::Advance(float dt) {
        if (!(dt > 0.0f) || _walking) {
            return;  // also NaN; a call from inside a callback would walk the list twice
        }
        _walking = true;
        // `_slots` is not resized during the walk (a start goes to `_pending`, a finish leaves a tombstone), so the count is fixed and the slot
        // reference that `Step` keeps across a setter and a callback stays valid
        const size_t count = _slots.size();
        for (size_t i = 0; i < count; ++i) {
            Slot& slot = _slots[i];
            if (IsLive(slot.state)) {
                Step(slot, dt);
            }
        }
        EndWalk();
    }

    void TweenManager::CancelAll() {
        const bool nested = _walking;
        _walking = true;
        // By index and to the sizes at the start: a callback may start a tween, which goes to the end of `_pending` and never moves `_slots`.
        // Called from inside a walk, `_pending` may hold tweens already: they are cancelled too, the walk that called us compacts
        const size_t slotCount = _slots.size();
        const size_t pendingCount = _pending.size();
        for (size_t i = 0; i < slotCount; ++i) {
            if (IsLive(_slots[i].state)) {
                CancelTween(_slots[i].id);
            }
        }
        for (size_t i = 0; i < pendingCount; ++i) {
            if (IsLive(_pending[i].state)) {
                CancelTween(_pending[i].id);
            }
        }
        if (!nested) {
            EndWalk();
        }
    }

    void TweenManager::Compact() {
        size_t kept = 0;
        for (size_t i = 0; i < _slots.size(); ++i) {
            Slot& slot = _slots[i];
            if (!IsLive(slot.state)) {
                _freeIds.push_back(slot.id);
                continue;
            }
            if (kept != i) {
                _slots[kept] = std::move(slot);
            }
            _indexOfId[_slots[kept].id] = static_cast<uint32_t>(kept);
            ++kept;
        }
        _slots.erase(_slots.begin() + static_cast<ptrdiff_t>(kept), _slots.end());
        _tombstones = 0;
    }

    void TweenManager::MergePending() {
        for (Slot& slot : _pending) {
            if (!IsLive(slot.state)) {
                _freeIds.push_back(slot.id);
                continue;
            }
            _indexOfId[slot.id] = static_cast<uint32_t>(_slots.size());
            _slots.push_back(std::move(slot));
        }
        _pending.clear();
    }

    void TweenManager::EndWalk() {
        _walking = false;
        // Lazy: a frame in which a few of thousands of tweens end must not shift the rest. A tombstone costs one state byte in the walk
        if (_tombstones != 0 && (_tombstones == _slots.size() || _tombstones >= eastl::max(kMinTombstones, _slots.size() / kTombstoneShare))) {
            Compact();
        }
        MergePending();
    }

    void TweenManager::Clear() {
        ++_clearCount;
        // First the bookkeeping of every tween, then the destruction of what they hold: a destructor (a `ScopedTween` in a capture, a handle in a
        // callback) may reach any other tween of this list, which has to read as finished by then
        Vector<IntrusivePtr<TweenHandle>> owners;
        Vector<TweenApply> applies;
        owners.reserve(_slots.size() + _pending.size());
        applies.reserve(owners.capacity());
        const auto drop = [&](Slot& slot) {
            if (!IsLive(slot.state)) {
                return;
            }
            Retired retired = Retire(slot, TweenState::Cancelled);
            retired.owner->_waiters.clear();  // never resumed: the coroutine may use what is gone already
            owners.push_back(std::move(retired.owner));
            applies.push_back(std::move(retired.apply));
        };
        for (Slot& slot : _slots) {
            drop(slot);
        }
        for (Slot& slot : _pending) {
            drop(slot);
        }
        if (!_walking) {
            _slots.clear();
            _pending.clear();
            _owners.clear();
            _indexOfId.clear();
            _freeIds.clear();
            _tombstones = 0;
        }  // else the walk in progress holds indices into the list: EndWalk removes what is finished
        for (size_t i = 0; i < owners.size(); ++i) {
            applies[i] = nullptr;
            owners[i]->_onComplete = nullptr;
            owners[i]->_onCancel = nullptr;
        }
    }

    size_t TweenManager::GetActiveCount() const {
        const auto live = [](const Slot& slot) { return IsLive(slot.state); };
        return static_cast<size_t>(eastl::count_if(_slots.begin(), _slots.end(), live) + eastl::count_if(_pending.begin(), _pending.end(), live));
    }

    size_t TweenManager::GetActiveCount(AnimationFrameClass frameClass) const {
        const auto live = [frameClass](const Slot& slot) { return IsLive(slot.state) && slot.frameClass == frameClass; };
        return static_cast<size_t>(eastl::count_if(_slots.begin(), _slots.end(), live) + eastl::count_if(_pending.begin(), _pending.end(), live));
    }

}  // namespace BECore
