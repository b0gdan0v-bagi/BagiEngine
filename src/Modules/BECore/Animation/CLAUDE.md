# Animation Module Guidelines

## Overview

Change a value over time: easing curves, interpolation and a manager that runs tweens. Pure maths plus one main-thread service, no renderer and no scene in it. `CoreManager::GetTweens()` is the one instance of the game, ticked once per frame (frame stage 2b, `BECore/CLAUDE.md`, "Frame order and time").

| File | Purpose |
|------|---------|
| `Easing.h/.cpp` | `EaseType` (`CORE_ENUM`: Linear, Quad / Cubic / Sine In / Out / InOut, Back In / Out / InOut, BounceOut, ElasticOut: the easings.net set) and `Ease(type, t)`. `t` is clamped to 0..1 (NaN gives 0) and the endpoints are exact for every type (`Ease(type, 0) == 0`, `Ease(type, 1) == 1`); Back and Elastic leave 0..1 in between, the others stay inside |
| `Lerp.h` | `Lerp(from, to, t)` for `float`, `Vec2` (`Math/Vec2.h`) and `Color` (per channel, rounded, clamped to 0..255); `t` is not clamped, so an overshoot reaches its bulge. The concept `Tweenable`: `Lerp(a, b, t)` exists and gives `T`; a game type adds its own `Lerp` |
| `Tween.h` | `TweenSettings` (`duration`, `delay`, `ease`), `TweenState` (Waiting, Running, Completed, Cancelled), `TweenHandle` (`GetState`, `IsFinished`, `IsCancelled`, `GetProgress`, `Cancel`, and the waiters the awaiter uses: `AddWaiter`, `RemoveWaiter`), `TweenResult` |
| `TweenManager.h/.cpp` | `Start<T>(from, to, settings, onValue, onComplete, onCancel)`, `Delay(seconds, onComplete, onCancel)` (a tween with no setter), `Wait(seconds)` (awaitable pause), `Advance(dt)`, `CancelAll()`, `Clear()`, `GetActiveCount()`. One type-erased list, so no code per value type |
| `TweenAwaiter.h` | `co_await AwaitTween(handle)` gives a `TweenResult` (`Completed`, `Cancelled`); the coroutine is resumed from `Advance`, on game time. Only `<coroutine>` and `Tween.h`, so no dependency on the task system |
| `ScopedTween.h` | Move-only owner of one handle; the destructor and `Reset` cancel (the cancel callback runs) |

## Rules (the tests in `tests/BECore/TweenManagerTest.cpp` pin them)

1. Tweens advance in the order they were started; the callbacks of one `Advance` fire in that order.
2. A tween started inside a callback (a chain) is first advanced by the NEXT `Advance`, never in the same one, so a callback cannot recurse or loop within a frame. The time left over after a tween finishes is dropped, not carried: a chain costs up to one frame per link.
3. The setter is not called during the delay. The `Advance` that passes the delay applies the time left after it, so the first call can already be mid-way. `from` is not applied before the first setter call.
4. The last setter call of a tween that finishes is exactly `to`, then the completion callback runs once. Never both completion and cancel.
5. A duration of zero or less completes on the first `Advance` after the delay (setter(to), then completion). A `dt` that is zero, negative or NaN advances nothing. `Advance` called from a callback that runs during `Advance` or `CancelAll` is ignored; a cancel callback run by a direct `TweenHandle::Cancel()` outside a walk may call `Advance` and it advances.
6. `Cancel()` on a running or waiting tween runs the cancel callback once, at once, and the tween gets no more setter calls. On a finished tween it does nothing. Cancelling from inside a callback (the same tween or another) is safe. Cancel does not set the value.
7. `CancelAll()` cancels with callbacks, in start order. `Clear()` drops every tween WITHOUT callbacks (shutdown: what the callbacks point at may be gone); the manager's destructor does the same, and a handle that outlives it reads Cancelled and does nothing on `Cancel()`.
8. Main thread only (manager and handles), like the scene state. A handle stays valid after the tween ends. Dropping a handle does not stop the tween (fire and forget, like `TaskHandle`). A setter that captures a pointer to an object must be cancelled before the object dies: hold a `ScopedTween` in the owner.
9. A coroutine that awaits a tween (`AwaitTween`, `Wait`) is a waiter. When the tween finishes, the waiters are resumed inline, in the order they registered, right after the `onComplete` / `onCancel` callback, and have all the rules of a callback: a tween it starts first runs on the NEXT `Advance` (so a chain of awaits costs one frame per link, rule 2), it may cancel others, and an `Advance` call from it is ignored while the walk is on. The result is `Cancelled` when the tween was cancelled. A waiter resumed by a `ScopedTween` destructor runs INSIDE that destructor: it checks the result and returns before it touches the owner. The await resumes on game time (the `dt` of `Advance`), never from `TaskSystem`: `DelaySec` / `DelayMs` are wall-clock jobs on the pool.
10. `Clear()` and the destructor of the manager never resume a waiter (what the coroutine uses may be gone; `CoreManager` clears before `TaskManager::Shutdown`, which abandons the frames). A `Task` dropped while it waits is never resumed later: the awaiter unregisters in its destructor.

`CoreManager::OnGameCycle` calls `Advance(Time::GetDeltaTime())` after `TaskManager::Update` and before `SceneManager::UpdateAll`, so values are set before the logic of the frame reads them. The frame time is clamped to `Time::kMaxFrameTime` (0.25 s), so a pause does not finish every tween at once; the manual clock gives the same ticks (`OnIdleCycle` runs no frame and advances nothing). `OnApplicationDeinit` and `OnTestEnvironmentDeinit` call `Clear()` first, before the task system stops and the scenes go.

## How to use

```cpp
// a component that slides its node
class SlideComponent : public IComponent {
    BE_CLASS(SlideComponent)
public:
    void Slide(float toX) {
        // GetComponent returns an IntrusivePtr by value, and it is null when the node has no such component
        if (auto transform = GetNode().GetComponent<TransformComponent>()) {
            const float fromX = transform->_x;
            // The setter holds the transform by IntrusivePtr (a copy in the capture), so it cannot dangle and needs no lookup per frame
            _slide.Reset(CoreManager::GetTweens().Start<float>(
                fromX, toX, {.duration = 0.2f, .ease = EaseType::QuadOut}, [transform](const float& x) { transform->_x = x; }));
        }
    }

private:
    ScopedTween _slide;  // the destructor cancels it, so nothing keeps writing once the component is gone (rule 8)
};
```

- Keep a `ScopedTween` (or an `IntrusivePtr<TweenHandle>` you cancel by hand) as a member of the object that owns the tween (and of the object the setter points at, when it captures a raw pointer). `IComponent` has no detach hook: the destructor of the component is where it is cancelled. A new `Reset` cancels the previous tween, so a second `Slide` replaces the first.
- Cancel does not set the value: when the object must end at the target, set it yourself, or let the tween finish.
- A chain (`onComplete` starts the next tween, or a coroutine awaits one tween after another) costs one frame per link; a timeline would do better (not there yet).
- A coroutine on the main thread (it must run there: the awaiter takes no lock), e.g. a pause and a slide:

```cpp
CoreManager::GetTaskManager().RunOnMainThread([]() -> Task<void> {
    TweenManager& tweens = CoreManager::GetTweens();
    co_await tweens.Wait(0.5f);  // 0.5 s of game time, resumed from Advance
    const TweenResult result = co_await AwaitTween(tweens.Start<float>(0.0f, 1.0f, {.duration = 0.2f}, [](const float& x) { /* write x */ }));
    if (result == TweenResult::Cancelled) {
        co_return;  // cancelled (also by a ScopedTween destructor, which resumes us inside it): touch no owner now
    }
    /* ... */
}());
```
- Tests: make a `TweenManager` of your own and drive it with `Advance(dt)`, or `CoreManager::GetTime().Advance(1.0 / 32)` and `Advance(GetTime().GetDeltaTime())`. Use frame times and durations that are powers of two so the sums are exact (`tests/BECore/TimeTest.cpp`). The tests of the maths (`EasingTest`, `LerpTest`) need no engine services.

## Not there yet

Timelines (sequence, parallel, delay, loops, yoyo) and the first consumer (Match3 swap, fall and refill) are later slices of the entry `no-animation`; `Vec3` and any 3D type are not there (nothing in the engine is 3D).
