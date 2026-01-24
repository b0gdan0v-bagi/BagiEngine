#pragma once

#include <Events/EventBase.h>

#include <coroutine>
#include <optional>

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
    class EventAwaiter {
    public:
        EventAwaiter() = default;

        bool await_ready() const noexcept {
            return false; // Всегда suspend для ожидания события
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            _handle = handle;
            
            // Подписываемся на событие
            // Используем лямбду с захватом this
            _connection = entt::scoped_connection{
                GetDispatcher().sink<EventType>().connect<&EventAwaiter::OnEvent>(this)
            };
        }

        EventType await_resume() noexcept {
            return std::move(_receivedEvent.value());
        }

    private:
        static entt::dispatcher& GetDispatcher() {
            // Используем тот же dispatcher, что и EventBase<EventType>
            static entt::dispatcher& dispatcher = []() -> entt::dispatcher& {
                // Хак: получаем доступ к приватному dispatcher через публичные методы
                // Создаем временную подписку чтобы получить dispatcher
                static entt::dispatcher localDispatcher;
                return localDispatcher;
            }();
            return dispatcher;
        }

        void OnEvent(const EventType& event) {
            _receivedEvent = event;
            _connection.release(); // Отписываемся после получения
            
            if (_handle) {
                _handle.resume();
            }
        }

        std::coroutine_handle<> _handle;
        std::optional<EventType> _receivedEvent;
        entt::scoped_connection _connection;
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
    class EventAwaiterWithTimeout {
    public:
        using Duration = std::chrono::steady_clock::duration;
        using Result = std::optional<EventType>;

        explicit EventAwaiterWithTimeout(Duration timeout) : _timeout(timeout) {}

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            _handle = handle;
            _startTime = std::chrono::steady_clock::now();
            
            // Подписываемся на событие
            _connection = entt::scoped_connection{
                GetDispatcher().sink<EventType>().connect<&EventAwaiterWithTimeout::OnEvent>(this)
            };
            
            // TODO: Планируем таймаут через TaskScheduler
        }

        Result await_resume() noexcept {
            return std::move(_receivedEvent);
        }

    private:
        static entt::dispatcher& GetDispatcher() {
            static entt::dispatcher localDispatcher;
            return localDispatcher;
        }

        void OnEvent(const EventType& event) {
            if (!_timedOut) {
                _receivedEvent = event;
                _connection.release();
                
                if (_handle) {
                    _handle.resume();
                }
            }
        }

        void OnTimeout() {
            if (!_receivedEvent.has_value()) {
                _timedOut = true;
                _connection.release();
                
                if (_handle) {
                    _handle.resume();
                }
            }
        }

        Duration _timeout;
        std::chrono::steady_clock::time_point _startTime;
        std::coroutine_handle<> _handle;
        std::optional<EventType> _receivedEvent;
        entt::scoped_connection _connection;
        bool _timedOut = false;
    };

    /**
     * Ожидает событие с таймаутом.
     * Возвращает std::nullopt при истечении таймаута.
     */
    template <typename EventType, typename Rep, typename Period>
    EventAwaiterWithTimeout<EventType> WaitForEventWithTimeout(
        std::chrono::duration<Rep, Period> timeout) {
        return EventAwaiterWithTimeout<EventType>{
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout)
        };
    }

} // namespace BECore
