#pragma once

#include <BECore/Animation/Tween.h>
#include <coroutine>

namespace BECore {

    /**
     * @brief `co_await` of a tween: suspends the coroutine until the tween completes or is cancelled, then gives the `TweenResult`.
     *
     * **Game time.** The coroutine is resumed from inside `TweenManager::Advance`, so on the thread that calls it (the main thread) and
     * on the frame clock (`Time::GetDeltaTime()`, clamped to `Time::kMaxFrameTime`; the same ticks on the manual clock). It is never
     * resumed by the wall-clock scheduler of the task system: `DelaySec` / `DelayMs` are `steady_clock` jobs that ignore the frame clamp
     * and the manual clock, so they are the wrong tool for animation. Use `TweenManager::Wait` for a pause.
     *
     * **Where the coroutine runs.** Main thread only: the awaiter touches the handle without a lock. A coroutine that awaits has to run
     * there (`TaskManager::RunOnMainThread`, or `co_await SwitchToMainThread()` first).
     *
     * **The resume is a callback.** The coroutine continues inline, right after the `onComplete` / `onCancel` of the tween and after the
     * waiters that registered before it, with all the rules of a callback: a tween it starts first runs on the NEXT `Advance`, it may
     * cancel others, and an `Advance` call from it is ignored while the walk is on. The time left over after the tween is dropped (a
     * chain of awaits costs one frame per link, rule 2 of `TweenManager`).
     *
     * **Cancelled.** A tween that is cancelled (`Cancel`, `CancelAll`, a `ScopedTween`) resumes the coroutine with `Cancelled`. A tween
     * cancelled by the destructor of its `ScopedTween` resumes it INSIDE that destructor: the coroutine has to check the result and
     * return before it touches the owner.
     *
     * **`Clear` never resumes.** `TweenManager::Clear()` and its destructor (shutdown) drop the waiters without resuming them, because
     * what the coroutine uses may be gone: its frame is freed when its `Task` is dropped. The awaiter unregisters itself in its
     * destructor, so a coroutine frame destroyed while it waits is never resumed later.
     *
     * Not copyable and not movable: the handle keeps its address while the coroutine waits.
     */
    class TweenAwaiter {
    public:
        explicit TweenAwaiter(IntrusivePtr<TweenHandle> handle) : _handle(std::move(handle)) {}

        ~TweenAwaiter() {
            if (_handle) {
                _handle->RemoveWaiter(this);
            }
        }

        TweenAwaiter(const TweenAwaiter&) = delete;
        TweenAwaiter& operator=(const TweenAwaiter&) = delete;
        TweenAwaiter(TweenAwaiter&&) = delete;
        TweenAwaiter& operator=(TweenAwaiter&&) = delete;

        /// True when the tween is finished already (or there is none): the coroutine does not suspend
        [[nodiscard]] bool await_ready() const noexcept {
            return !_handle || _handle->IsFinished();
        }

        void await_suspend(std::coroutine_handle<> continuation) {
            _handle->AddWaiter(this, continuation);
        }

        /// `Cancelled` when the tween was cancelled (or there was none), else `Completed`
        [[nodiscard]] TweenResult await_resume() const noexcept {
            return _handle && !_handle->IsCancelled() ? TweenResult::Completed : TweenResult::Cancelled;
        }

    private:
        IntrusivePtr<TweenHandle> _handle;
    };

    /// `co_await AwaitTween(handle)`: waits for the tween on game time, see `TweenAwaiter`
    [[nodiscard]] inline TweenAwaiter AwaitTween(IntrusivePtr<TweenHandle> handle) {
        return TweenAwaiter(std::move(handle));
    }

}  // namespace BECore
