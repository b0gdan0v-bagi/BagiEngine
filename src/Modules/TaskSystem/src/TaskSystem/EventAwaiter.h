#pragma once

#include <Events/EventBase.h>
#include <Events/SubscriptionHolder.h>
#include <chrono>
#include <coroutine>

namespace BECore {

    /**
     * EventAwaiter<T> - awaitable для ожидания события.
     *
     * Позволяет использовать co_await для ожидания конкретного события.
     * При получении события корутина возобновляется с данными события.
     *
     * @tparam EventType Тип события (должен наследовать EventBase<EventType>)
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
            return false;  // Всегда suspend для ожидания события
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
     * Ожидает получение события указанного типа.
     *
     * @tparam EventType Тип события
     * @return Awaitable, который вернет полученное событие
     *
     * @example
     * auto keyEvent = co_await WaitForEvent<KeyPressEvent>();
     */
    template <typename EventType>
    EventAwaiter<EventType> WaitForEvent() {
        return EventAwaiter<EventType>{};
    }

    /**
     * EventAwaiterWithTimeout - ожидание события с таймаутом.
     *
     * @tparam EventType Тип события
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

            // The timeout is not scheduled yet (there is no scheduler to call OnTimeout), so this awaiter waits for the event only.
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
     * Ожидает событие с таймаутом.
     * Возвращает std::nullopt при истечении таймаута.
     */
    template <typename EventType, typename Rep, typename Period>
    EventAwaiterWithTimeout<EventType> WaitForEventWithTimeout(std::chrono::duration<Rep, Period> timeout) {
        return EventAwaiterWithTimeout<EventType>{std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout)};
    }

}  // namespace BECore
