# Animation Module Guidelines

## Overview

Change a value over time: easing curves, interpolation, a manager that runs tweens, timelines of clips and `co_await` of a tween. Pure maths plus one main-thread service, no renderer and no scene in it. `CoreManager::GetTweens()` is the one instance of the game, ticked once per frame (frame stage 2b, `BECore/CLAUDE.md`, "Frame order and time").

| File | Purpose |
|------|---------|
| `Easing.h/.cpp` | `EaseType` (`CORE_ENUM`: Linear, Quad / Cubic / Sine In / Out / InOut, Back In / Out / InOut, BounceOut, ElasticOut: the easings.net set) and `Ease(type, t)`. `t` is clamped to 0..1 (NaN gives 0) and the endpoints are exact for every type (`Ease(type, 0) == 0`, `Ease(type, 1) == 1`); Back and Elastic leave 0..1 in between, the others stay inside |
| `Lerp.h` | `Lerp(from, to, t)` for `float`, `Vec2` (`Math/Vec2.h`) and `Color` (per channel, rounded, clamped to 0..255); `t` is not clamped, so an overshoot reaches its bulge. The concept `Tweenable`: `Lerp(a, b, t)` exists and gives `T`; a game type adds its own `Lerp` |
| `Tween.h` | `TweenSettings` (`duration`, `delay`, `ease`), `TweenState` (Waiting, Running, Completed, Cancelled), `TweenHandle` (`GetState`, `IsFinished`, `IsCancelled`, `GetProgress`, `Cancel`, and the waiters the awaiter uses: `AddWaiter`, `RemoveWaiter`), `TweenResult` |
| `TweenManager.h/.cpp` | `Start<T>(from, to, settings, onValue, onComplete, onCancel)`, `Play(timeline, delay, onComplete, onCancel)` (a `Timeline` as one tween), `Delay(seconds, onComplete, onCancel)` (a tween with no setter), `Wait(seconds)` (awaitable pause), `Advance(dt)`, `CancelAll()`, `Clear()`, `GetActiveCount()`. One type-erased list, so no code per value type |
| `TweenAwaiter.h` | `co_await AwaitTween(handle)` gives a `TweenResult` (`Completed`, `Cancelled`); the coroutine is resumed from `Advance`, on game time. Only `<coroutine>` and `Tween.h`, so no dependency on the task system |
| `Timeline.h/.cpp` | `Timeline`: a set of clips (tweens, delays, callbacks, sub-timelines) on one local time line. Builder `Append` / `Join` / `Insert(at)` (typed tween clips and sub-timelines), `AppendDelay`, `AppendCallback` / `InsertCallback(at)`, `Repeat(count)`; `Evaluate(time)`, `GetDuration()`, `IsFinished()`, `Reset()`, `SetStopCheck(predicate)` (set by `Play`: `Evaluate` stops at the clip boundary once it answers true, rule 15). Pure maths: no manager, no clock. Move-only. Run it with `TweenManager::Play` |
| `ScopedTween.h` | Move-only owner of one handle; the destructor and `Reset` cancel (the cancel callback runs) |

## Rules (the tests in `tests/BECore/TweenManagerTest.cpp`, `TweenAwaiterTest.cpp`, `TimelineTest.cpp` and `TimelinePlayTest.cpp` pin them)

1. Tweens advance in the order they were started; the callbacks of one `Advance` fire in that order.
2. A tween started inside a callback (a chain) is first advanced by the NEXT `Advance`, never in the same one, so a callback cannot recurse or loop within a frame. The time left over after a tween finishes is dropped, not carried: a chain costs up to one frame per link.
3. The setter is not called during the delay. The `Advance` that passes the delay applies the time left after it, so the first call can already be mid-way. `from` is not applied before the first setter call.
4. The last setter call of a tween that finishes is exactly `to`, then the completion callback runs once. Never both completion and cancel.
5. A duration of zero or less completes on the first `Advance` after the delay (setter(to), then completion). A `dt` that is zero, negative or NaN advances nothing. `Advance` called from a callback that runs during `Advance` or `CancelAll` is ignored; a cancel callback run by a direct `TweenHandle::Cancel()` outside a walk may call `Advance` and it advances.
6. `Cancel()` on a running or waiting tween runs the cancel callback once, at once, and the tween gets no more setter calls. On a finished tween it does nothing. Cancelling from inside a callback (the same tween or another) is safe. Cancel does not set the value.
7. `CancelAll()` cancels with callbacks, in start order. `Clear()` drops every tween WITHOUT callbacks (shutdown: what the callbacks point at may be gone); the manager's destructor does the same, and a handle that outlives it reads Cancelled and does nothing on `Cancel()`.
8. Main thread only (manager and handles), like the scene state. A handle stays valid after the tween ends. Dropping a handle does not stop the tween (fire and forget, like `TaskHandle`). A setter that captures a pointer to an object must be cancelled before the object dies: hold a `ScopedTween` in the owner.
9. A coroutine that awaits a tween (`AwaitTween`, `Wait`) is a waiter. When the tween finishes, the waiters are resumed inline, in the order they registered, right after the `onComplete` / `onCancel` callback, and have all the rules of a callback: a tween it starts first runs on the NEXT `Advance` (so a chain of awaits costs one frame per link, rule 2), it may cancel others, and an `Advance` call from it is ignored while the walk is on. The result is `Cancelled` when the tween was cancelled. A waiter resumed by a `ScopedTween` destructor runs INSIDE that destructor: it checks the result and returns before it touches the owner. The await resumes on game time (the `dt` of `Advance`), never from `TaskSystem`: `DelaySec` / `DelayMs` are wall-clock jobs on the pool. The task-system counterpart on game time is `DelayGameSec` / `DelayGameMs` (fed by `CoreManager::OnGameCycle`).
10. `Clear()` and the destructor of the manager never resume a waiter (what the coroutine uses may be gone; `CoreManager` clears before `TaskManager::Shutdown`, which abandons the frames). A `Task` dropped while it waits is never resumed later: the awaiter unregisters in its destructor.

**Timelines** (`Timeline`, run by `TweenManager::Play`; the rules 1 to 10 hold for the handle of a played timeline, and rule 15 says what a cancel from inside a clip does, because `Play` is one linear tween of `GetDuration()` seconds whose step evaluates the timeline at `progress * duration`):

11. A timeline is evaluated by TIME, not clip by clip: every clip has a start and a length on one local time line, so the time between clips is exact. A `Play` of four clips of 0.25 s at a frame time of 0.375 s ends in three `Advance` calls with every clip finished in order, where four tweens chained by `onComplete` need four (rule 2 drops the left-over time of every link).
12. `Evaluate(time)` takes a time that never goes back (`Reset()` starts over). A clip that is entered gets a setter call on every `Evaluate` while the time is inside it; a clip the time has passed gets exactly ONE final `setter(to)`, never skipped, so a big `dt` leaves every value right (two clips on one property: the later one in the order wins); a callback fires once when the time reaches it, also when one `Evaluate` jumps over several. The call at `time >= GetDuration()` finishes everything, and the last step of `Play` is exactly the duration.
13. `Repeat(count)` runs the whole timeline `count` times forward (finite; below 1 is 1). An iteration finishes, the next one starts every clip from its `from` again and goes on with the left-over time. `GetDuration()` includes the repeats. A timeline with no length is done by its first call and does not loop. Yoyo and an infinite repeat are not there.
14. A clip has no cancel callback: cancelling the handle of a played timeline stops it where it is (no value is set), and the `onCancel` of `Play` is the one callback of the run. The `delay` of a clip's `TweenSettings` lengthens its slot (the clip starts after it, no setter call during it).
15. Cancelling from inside a clip of the same run (a callback clip calls `Cancel()`, `CancelAll()`, `Clear()`, resets or destroys its `ScopedTween`, or the owner destroys itself) stops the timeline at that clip: `Play` gives the timeline a stop check (`Timeline::SetStopCheck`, the handle `IsFinished()`), `Evaluate` asks it after every clip, also inside sub-timelines and between repeats, and returns. The clip that cancelled finishes its own call, then no setter or callback runs, so rule 8 (an owner with a `ScopedTween` whose setters capture `this`) holds for timelines. The `onCancel` and the waiters run INSIDE the clip that cancelled (rule 6, 9), not after the `Evaluate`; a waiter or `onCancel` that touches the owner must not assume the clip is over.

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
- A chain (`onComplete` starts the next tween, or a coroutine awaits one tween after another) costs one frame per link; a `Timeline` carries the time and does not.
- A timeline: a sequence with a tween in parallel and a callback, played as one tween (the handle is cancelled, awaited and held by a `ScopedTween` like any other):

```cpp
Timeline timeline;
timeline.Append<float>(0.0f, 1.0f, {.duration = 0.2f, .ease = EaseType::QuadOut}, [this](const float& alpha) { _alpha = alpha; })  // 0.0 .. 0.2
    .Join<Vec2>(startPos, endPos, {.duration = 0.2f}, [this](const Vec2& pos) { _pos = pos; })                              // with the one above
    .AppendDelay(0.1f)                                                                                                                    // 0.2 .. 0.3
    .AppendCallback([]() { /* at 0.3 */ });
_play.Reset(CoreManager::GetTweens().Play(std::move(timeline), 0.0f, [] { /* done */ }));  // _play is a ScopedTween member, like in the slide above
```

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
  The same for a timeline: `co_await AwaitTween(tweens.Play(std::move(timeline)))`. The example is not compiled by any test (`TweenAwaiterTest` and `TimelinePlayTest` drive bare `Task`s with `Resume()` and `Advance`); the wiring through the real frame (`RunOnMainThread` and the frame loop) has no gtest either, because `TaskManager::Update` takes `PassKey<CoreManager>`.
- Tests: make a `TweenManager` of your own and drive it with `Advance(dt)`, or `CoreManager::GetTime().Advance(1.0 / 32)` and `Advance(GetTime().GetDeltaTime())`. Use frame times and durations that are powers of two so the sums are exact (`tests/BECore/TimeTest.cpp`). The tests that need no engine services are `EasingTest`, `LerpTest`, `TimelineTest`, `TweenAwaiterTest` and `TimelinePlayTest` (a `TweenManager` of their own and `Advance`).

## Not there yet

Yoyo and an infinite repeat of a timeline (a finite `Repeat` is there; nothing needs a pulse yet). `Vec3` and any 3D type are not there (nothing in the engine is 3D). A clip has no cancel callback of its own (rule 14). The first consumer is there: the swap, fall and refill of `games/Match3` (`BoardAnimation` on `CoreManager::GetTweens()`; `games/Match3/CLAUDE.md`, "Animation").
