# Animation Module Guidelines

## Overview

Change a value over time: easing curves, interpolation, a manager that runs tweens, timelines of clips and `co_await` of a tween. Pure maths plus one main-thread service, no renderer and no scene in it. `CoreManager::GetTweens()` is the one instance of the game, ticked once per frame (frame stage 2b, `BECore/CLAUDE.md`, "Frame order and time").

| File | Purpose |
|------|---------|
| `Easing.h/.cpp` | `EaseType` (`CORE_ENUM`: Linear, Quad / Cubic / Sine In / Out / InOut, Back In / Out / InOut, BounceOut, ElasticOut: the easings.net set) and `Ease(type, t)`. `t` is clamped to 0..1 (NaN gives 0) and the endpoints are exact for every type (`Ease(type, 0) == 0`, `Ease(type, 1) == 1`); Back and Elastic leave 0..1 in between, the others stay inside |
| `Lerp.h` | `Lerp(from, to, t)` for `float`, `Vec2` (`Math/Vec2.h`) and `Color` (per channel, rounded, clamped to 0..255; a NaN `t` gives 0); `t` is not clamped, so an overshoot reaches its bulge. The concept `Tweenable`: `Lerp(a, b, t)` exists and gives `T`; a game type adds its own `Lerp` |
| `Tween.h` | `TweenSettings` (`duration`, `delay`, `ease`), `TweenApply` (the type-erased setter: a `FixedFunction<kTweenApplyBytes, void(float)>`, so it never allocates and a typed `onValue` whose capture does not fit is a compile error with a message) and `MakeTweenApply(from, to, onValue)` (the one builder of it, shared by `TweenManager::Start` and the clips of `Timeline`; `{}` or `nullptr` for `onValue` is a setter that does nothing), `TweenState` (Waiting, Running, Completed, Cancelled), `AnimationFrameClass` (`CORE_ENUM`: Interactive, FiniteEffect, DecorativeLoop; the meaning of `ParticleFrameClass`, rule 16), `TweenHandle` (`GetState`, `IsFinished`, `IsCancelled`, `GetProgress`, `GetFrameClass`, `Cancel`, and the waiters the awaiter uses: `AddWaiter`, `RemoveWaiter`), `TweenResult` |
| `TweenManager.h/.cpp` | `Start<T>(from, to, settings, onValue, onComplete, onCancel)`, `Play(timeline, delay, onComplete, onCancel)` (a `Timeline` as one tween, an endless one a handle that only a cancel ends, rule 16; the setter owns it by `UniquePtr`), `Delay(seconds, onComplete, onCancel)` (a tween with no setter), `Wait(seconds)` (awaitable pause), `Advance(dt)`, `CancelAll()`, `Clear()`, `GetActiveCount()` and `GetActiveCount(AnimationFrameClass)`. One type-erased list, so no code per value type. `onComplete` / `onCancel` are `MoveOnlyFunction<void()>`: a move-only capture works, and the three callables of a tween are inline in its handle |
| `TweenAwaiter.h` | `co_await AwaitTween(handle)` gives a `TweenResult` (`Completed`, `Cancelled`); the coroutine is resumed from `Advance`, on game time. Only `<coroutine>` and `Tween.h`, so no dependency on the task system |
| `Timeline.h/.cpp` | `Timeline`: a set of clips (tweens, delays, callbacks, sub-timelines) on one local time line. Builder `Append` / `Join` / `Insert(at)` (typed tween clips and sub-timelines), `AppendDelay`, `AppendCallback` / `InsertCallback(at)`, `Repeat(count)`, `RepeatForever()`, `Yoyo()`, `SetFrameClass` (`GetFrameClass`); `Evaluate(time)` (a time since the start) and `Advance(dt)` (its own clock, exact for a run of any length; never mixed on one run), `GetDuration()`, `IsEndless()`, `IsFinished()`, `Reset()`, `SetStopCheck(predicate)` (set by `Play`: `Evaluate` stops at the clip boundary once it answers true, rule 15; the check of the timeline `Evaluate` is called on governs its sub-timelines). Pure maths: no manager, no clock. Move-only. Run it with `TweenManager::Play` |
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
9. A coroutine that awaits a tween (`AwaitTween`, `Wait`) is a waiter. When the tween finishes, the waiters are resumed inline, in the order they registered, right after the `onComplete` / `onCancel` callback, and have all the rules of a callback: a tween it starts first runs on the NEXT `Advance` (so a chain of awaits costs one frame per link, rule 2), it may cancel others, and an `Advance` call from it is ignored while the walk is on. The result is `Cancelled` when the tween was cancelled. A waiter resumed by a `ScopedTween` destructor runs INSIDE that destructor: it checks the result and returns before it touches the owner. The await resumes on game time (the `dt` of `Advance`), never from `TaskSystem`: `DelaySec` / `DelayMs` are wall-clock jobs on the pool. The task-system counterpart on game time is `DelayGameSec` / `DelayGameMs` (fed by `CoreManager::OnGameCycle`). The first waiter of a handle lives inside it (`SmallVector<Waiter, 1>`), so awaiting a tween allocates nothing; a second waiter on the same handle takes one heap block.
10. `Clear()` and the destructor of the manager never resume a waiter (what the coroutine uses may be gone; `CoreManager` clears before `TaskManager::Shutdown`, which abandons the frames). A `Task` dropped while it waits is never resumed later: the awaiter unregisters in its destructor.

**Timelines** (`Timeline`, run by `TweenManager::Play`; the rules 1 to 10 hold for the handle of a played timeline, and rule 15 says what a cancel from inside a clip does, because `Play` is one linear tween of `GetDuration()` seconds whose step evaluates the timeline at `progress * duration`):

11. A timeline is evaluated by TIME, not clip by clip: every clip has a start and a length on one local time line, so the time between clips is exact. A `Play` of four clips of 0.25 s at a frame time of 0.375 s ends in three `Advance` calls with every clip finished in order, where four tweens chained by `onComplete` need four (rule 2 drops the left-over time of every link).
12. `Evaluate(time)` takes a time that never goes back (`Reset()` starts over). A clip that is entered gets a setter call on every `Evaluate` while the time is inside it; a clip the time has passed gets exactly ONE final `setter(to)`, never skipped, so a big `dt` leaves every value right (two clips on one property: the later one in the order wins); a callback fires once when the time reaches it, also when one `Evaluate` jumps over several. The call at `time >= GetDuration()` finishes everything, and the last step of `Play` is exactly the duration.
13. `Repeat(count)` runs the whole timeline `count` times (below 1 is 1), `RepeatForever()` without end; the last call of the two wins. An iteration finishes (every clip's exact final call and its callbacks), the next one starts every clip from its `from` again and goes on with the left-over time, so a call that spans several iterations crosses every one. `GetDuration()` includes the repeats and is `+infinity` for an endless timeline with length. A timeline with no length is done by its first call and does not loop, endless or not. An endless timeline is never `IsFinished()` until a `Reset()`. A parent cannot contain it: `Append`, `Join` and `Insert` of an endless sub-timeline play ONE iteration of it.
14. A clip has no cancel callback: cancelling the handle of a played timeline stops it where it is (no value is set), and the `onCancel` of `Play` is the one callback of the run. The `delay` of a clip's `TweenSettings` lengthens its slot (the clip starts after it, no setter call during it).
15. Cancelling from inside a clip of the same run (a callback clip calls `Cancel()`, `CancelAll()`, `Clear()`, resets or destroys its `ScopedTween`, or the owner destroys itself) stops the timeline at that clip: `Play` gives the timeline a stop check (`Timeline::SetStopCheck`, the handle `IsFinished()`), `Evaluate` asks it after every clip, also inside sub-timelines (the root's check governs them; one set on a sub before it was moved in is never asked) and between repeats, and returns. The clip that cancelled finishes its own call, then no setter or callback runs, so rule 8 (an owner with a `ScopedTween` whose setters capture `this`) holds for timelines. The `onCancel` and the waiters run INSIDE the clip that cancelled (rule 6, 9), not after the `Evaluate`; a waiter or `onCancel` that touches the owner must not assume the clip is over.
16. **Yoyo, endless runs, the frame class.** (a) `Yoyo()` makes every second iteration (index 1, 3, ...) the exact time reversal of a forward one: a tween writes `Lerp(from, to, Ease(type, 1 - p))` (the same curve walked back, a Back or Elastic bulge at the end of the back leg) and its last call is exactly `from`; clips are entered latest end first (equal ends in reverse insertion order), a clip's `delay` sits at the end of its backward slot, a callback fires once per iteration at the mirrored time (`duration - at`), a zero-length clip writes `to` forward and `from` backward. A sub-timeline is played reversed as a whole (its own repeats and yoyo too). A step that ends on a leg boundary also enters the next leg at its start, which writes the value the previous leg ended on. `Repeat(1)` or one iteration changes nothing. (b) `TweenManager::Play` of an endless timeline is a handle that runs until it is cancelled (`Cancel`, `CancelAll`, a `ScopedTween`): `onComplete` never runs, `GetProgress()` stays 0, an await ends only by cancel; the rules 1 to 10 and 15 hold. Its setter gets the seconds of the step (`Timeline::Advance`), the first one the time left after the delay (rule 3), so a run of hours keeps its precision; `Clear()` drops it without callbacks. (c) `AnimationFrameClass` is the hook of the render-on-demand entry: a `Start` or `Delay` tween is `FiniteEffect`, a played timeline has the class of the timeline (`SetFrameClass`; not set: `DecorativeLoop` when endless, else `FiniteEffect`), and `GetActiveCount(class)` lets the frame loop see that only endless decorative loops run and cap them. The loop itself is not here.

`CoreManager::OnGameCycle` calls `Advance(Time::GetDeltaTime())` after `TaskManager::Update` and before `SceneManager::UpdateAll`, so values are set before the logic of the frame reads them. The frame time is clamped to `Time::kMaxFrameTime` (0.25 s), so a pause does not finish every tween at once; the manual clock gives the same ticks (`OnIdleCycle` runs no frame and advances nothing). A held frame of a paused game (`GamePause`) advances no tween and no `Wait`: the tweens stand still until a step or the resume. `OnApplicationDeinit` and `OnTestEnvironmentDeinit` call `Clear()` first, before the task system stops and the scenes go.

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
- The setter never allocates (`TweenApply`): its capture, with `from` and `to`, must fit `kTweenApplyBytes` and move without throwing. A `const` captured object does not (the move copies it): capture a non-const copy (`[transform = node->transform]`), or a pointer to an object that holds the state. A callback (`onComplete`, a clip callback) may be larger: it takes one allocation.
- Cancel does not set the value: when the object must end at the target, set it yourself, or let the tween finish.
- A `Timeline` and its sub-timelines are heap objects (`TweenManager::Play`, `Timeline::AddSub`): nothing in the engine plays timelines, so there is no churn to pool. A game that plays one per frame opts `Timeline` into `BE_POOLED_SINGLE_THREAD` after measuring it (`Memory/CLAUDE.md`, "Choosing the next type").
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

- A loop that never ends (a pulse, a breathing icon): a yoyo with `RepeatForever()`, played once and stopped by its owner:

```cpp
Timeline pulse;
pulse.Append<float>(1.0f, 1.1f, {.duration = 0.5f, .ease = EaseType::SineInOut}, [this](const float& scale) { _scale = scale; }).Yoyo().RepeatForever();
_pulse.Reset(CoreManager::GetTweens().Play(std::move(pulse)));  // never completes; the ScopedTween member stops it (rule 8, 16)
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

`Vec3` and `Vec4` are `Tweenable` (`Math/`); a `Quat` or `Mat4` is not tweened. A clip has no cancel callback of its own (rule 14). The first consumer is there: the swap, fall and refill of the board of the first game (a board animation on `CoreManager::GetTweens()`).
