#pragma once

#include <Events/EventBase.h>
#include <Events/SubscriptionHolder.h>
#include <chrono>
#include <coroutine>

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

    /**
     * Awaits an event with a timeout; the timeout is not scheduled yet, so it waits for the event only.
     */
    template <typename EventType>
    class EventAwaiterWithTimeout : public SubscriptionHolder {
    public:
        using Duration = std::chrono::steady_clock::duration;
        using Result = Optional<EventType>;

        explicit EventAwaiterWithTimeout(Duration timeout) : _timeout(timeout) {}

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            _handle = handle;
            _startTime = std::chrono::steady_clock::now();

            Subscribe<EventType, &EventAwaiterWithTimeout::OnEvent>(this);
        }

        Result await_resume() noexcept {
            return std::move(_receivedEvent);
        }

    private:
        void OnEvent(const EventType& event) {
            if (!_timedOut) {
                _receivedEvent = event;
                UnsubscribeAll();

                if (const auto handle = _handle) {
                    handle.resume();
                }
            }
        }

        void OnTimeout() {
            if (!_receivedEvent.has_value()) {
                _timedOut = true;
                UnsubscribeAll();

                if (const auto handle = _handle) {
                    handle.resume();
                }
            }
        }

        Duration _timeout;
        std::chrono::steady_clock::time_point _startTime;
        std::coroutine_handle<> _handle;
        Optional<EventType> _receivedEvent;
        bool _timedOut = false;
    };

    /**
     * Event awaiter with a timeout (see EventAwaiterWithTimeout: the timeout does not fire yet).
     */
    template <typename EventType, typename Rep, typename Period>
    EventAwaiterWithTimeout<EventType> WaitForEventWithTimeout(std::chrono::duration<Rep, Period> timeout) {
        return EventAwaiterWithTimeout<EventType>{std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout)};
    }

}  // namespace BECore
