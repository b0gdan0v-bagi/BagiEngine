#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Memory/Pooled.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/New.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <Events/EventBase.h>
#include <Events/SubscriptionHolder.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <algorithm>
#include <chrono>
#include <coroutine>
#include <utility>

namespace BECore {

    /**
     * Awaits one event of `EventType` (an EventBase<EventType>): the coroutine resumes inline from the handler, on
     * the thread that fires the event, and the awaiter is unsubscribed first.
     *
     * @example
     * Task<void> HandleInput() {
     *     auto event = co_await WaitForEvent<KeyPressEvent>();
     *     LOG_INFO("Key pressed: {}"_format(event.keyCode));
     * }
     */
    template <typename EventType>
    class EventAwaiter : public SubscriptionHolder {
    public:
        EventAwaiter() = default;

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            _handle = handle;
            Subscribe<EventType, &EventAwaiter::OnEvent>(this);
        }

        EventType await_resume() noexcept {
            return std::move(_receivedEvent.value());
        }

    private:
        void OnEvent(const EventType& event) {
            _receivedEvent = event;
            UnsubscribeAll();

            // The coroutine frame owns this awaiter and may destroy it: nothing is touched after the resume.
            if (const auto handle = _handle) {
                handle.resume();
            }
        }

        std::coroutine_handle<> _handle;
        Optional<EventType> _receivedEvent;
    };

    /**
     * Awaits one event of `EventType`.
     *
     * @example
     * auto keyEvent = co_await WaitForEvent<KeyPressEvent>();
     */
    template <typename EventType>
    EventAwaiter<EventType> WaitForEvent() {
        return EventAwaiter<EventType>{};
    }

    namespace Detail {

        /**
         * What the scheduled timeout of an EventAwaiterWithTimeout holds in place of the awaiter. It outlives the awaiter in the scheduler's queue, so a
         * timeout that comes due after the coroutine is gone finds no owner. Not a template, so every event type shares one pool; the scheduler's thread
         * may release the last reference, hence BE_POOLED.
         */
        struct TimerLink : RefCountedAtomic {
            BE_POOLED(TimerLink)

            void* owner = nullptr;
            void (*finish)(void* owner) = nullptr;
        };

    }  // namespace Detail

    /**
     * Awaits one event of `EventType` for at most a wall-clock time: `co_await` gives the event, or an empty Optional when the time ran out first.
     *
     * The coroutine resumes once, by whichever comes first, and the other one then does nothing. Await and emit on the main thread: the timeout
     * is released by TaskManager::Update there, and the event channel belongs to one thread. A non-positive timeout does not suspend, and neither
     * does an await without a running TaskManager, which has nothing to release the timeout.
     */
    template <typename EventType>
    class EventAwaiterWithTimeout : public SubscriptionHolder {
    public:
        using Duration = std::chrono::steady_clock::duration;
        using Result = Optional<EventType>;

        /// A longer timeout waits this long: the scheduler adds the delay to the clock's current time, which must not overflow
        static constexpr Duration kMaxTimeout = std::chrono::duration_cast<Duration>(std::chrono::hours(24 * 365 * 100));

        explicit EventAwaiterWithTimeout(Duration timeout) : _timeout(std::min(timeout, kMaxTimeout)) {}

        // The scheduled timeout and the subscription both point at this object
        EventAwaiterWithTimeout(EventAwaiterWithTimeout&&) = delete;
        EventAwaiterWithTimeout& operator=(EventAwaiterWithTimeout&&) = delete;

        ~EventAwaiterWithTimeout() override {
            DetachTimer();
        }

        bool await_ready() const noexcept {
            return _timeout <= Duration::zero();
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            TaskScheduler* scheduler = TaskManager::GetInstance().GetScheduler();
            if (scheduler == nullptr) {
                return false;
            }
            ASSERT(TaskManager::GetInstance().IsMainThread());
            _handle = handle;
            Subscribe<EventType, &EventAwaiterWithTimeout::OnEvent>(this);
            _timer = New<Detail::TimerLink>();
            _timer->owner = this;
            _timer->finish = [](void* awaiter) { static_cast<EventAwaiterWithTimeout*>(awaiter)->Finish(); };
            scheduler->ScheduleDelayed(
                [timer = _timer]() {
                    if (timer->owner != nullptr) {
                        timer->finish(timer->owner);
                    }
                },
                _timeout, TaskPriority::Normal, ThreadType::MainThread);
            return true;
        }

        Result await_resume() noexcept {
            return std::move(_receivedEvent);
        }

    private:
        void OnEvent(const EventType& event) {
            _receivedEvent = event;
            Finish();
        }

        void Finish() {
            UnsubscribeAll();
            DetachTimer();

            // The coroutine frame owns this awaiter and may destroy it: nothing is touched after the resume.
            if (const auto handle = _handle) {
                handle.resume();
            }
        }

        void DetachTimer() {
            if (_timer) {
                _timer->owner = nullptr;
                _timer.Reset();
            }
        }

        Duration _timeout;
        std::coroutine_handle<> _handle;
        Optional<EventType> _receivedEvent;
        IntrusivePtrAtomic<Detail::TimerLink> _timer;
    };

    /**
     * Awaits one event of `EventType` for at most @p timeout (see EventAwaiterWithTimeout). A NaN counts as no time at all.
     *
     * @example
     * auto keyEvent = co_await WaitForEventWithTimeout<KeyPressEvent>(std::chrono::seconds(5));
     * if (!keyEvent) { LOG_INFO("Nobody pressed a key"); }
     */
    template <typename EventType, typename Rep, typename Period>
    EventAwaiterWithTimeout<EventType> WaitForEventWithTimeout(std::chrono::duration<Rep, Period> timeout) {
        using Awaiter = EventAwaiterWithTimeout<EventType>;
        using Seconds = std::chrono::duration<double>;
        // Compared as floating point: the conversion to the clock's integer ticks overflows for a very long timeout
        const Seconds seconds = std::chrono::duration_cast<Seconds>(timeout);
        if (!(seconds > Seconds::zero())) {
            return Awaiter{Awaiter::Duration::zero()};
        }
        if (seconds >= std::chrono::duration_cast<Seconds>(Awaiter::kMaxTimeout)) {
            return Awaiter{Awaiter::kMaxTimeout};
        }
        return Awaiter{std::chrono::duration_cast<typename Awaiter::Duration>(timeout)};
    }

}  // namespace BECore
