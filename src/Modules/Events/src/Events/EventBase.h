#pragma once

#include <Events/EventsQueueRegistry.h>
#include <Events/SubscriptionHolder.h>
#include <entt/entt.hpp>

namespace BECore {

    struct BaseEvent {
        virtual ~BaseEvent() = default;
    };

    // CRTP базовый класс для статических методов событий с индивидуальным dispatcher'ом
    template <typename Derived>
    struct EventBase : public BaseEvent {

        template <auto Candidate, typename Type>
        static entt::scoped_connection Subscribe(Type* instance, PassKey<SubscriptionHolder>) {
            return GetDispatcher().sink<Derived>().template connect<Candidate>(instance);
        }

        template <auto Candidate, typename Type>
        static void Unsubscribe(Type* instance) {
            GetDispatcher().sink<Derived>().template disconnect<Candidate>(instance);
        }

        /**
         * @brief Unsubscribe all handlers for a specific instance
         * @tparam Type Type of the instance
         * @param instance Pointer to the instance
         */
        template <typename Type>
        static void UnsubscribeAll(Type* instance) {
            GetDispatcher().sink<Derived>().disconnect(instance);
        }

        static void Emit() requires(std::is_default_constructible_v<Derived>) {
            GetDispatcher().trigger(Derived{});
        }

        template <typename... Args>
        static void Emit(Args&&... args) {
            GetDispatcher().trigger(Derived{std::forward<Args>(args)...});
        }

        static void Enqueue() requires(std::is_default_constructible_v<Derived>) {
            RegisterOnce();
            GetDispatcher().enqueue(Derived{});
        }

        template <typename... Args>
        static void Enqueue(Args&&... args) {
            RegisterOnce();
            GetDispatcher().enqueue(Derived{std::forward<Args>(args)...});
        }

        static void Update() {
            GetDispatcher().update();
        }

    private:
        static entt::dispatcher& GetDispatcher() {
            static entt::dispatcher dispatcher;
            return dispatcher;
        }

        static void RegisterOnce() {
            static bool registered = false;
            if (!registered) {
                registered = true;
                EventsQueueRegistry::Register([]() { Derived::Update(); }, PassKey<BaseEvent>{});
            }
        }
    };

}  // namespace BECore

