#pragma once

#include <BECore/Utils/PassKey.h>
#include <Events/EventChannel.h>
#include <Events/EventsQueueRegistry.h>
#include <Events/SubscriptionHolder.h>
#include <functional>
#include <type_traits>
#include <utility>

namespace BECore {

    struct BaseEvent {
        virtual ~BaseEvent() = default;

    protected:
        // PassKey<BaseEvent> can be made by BaseEvent only, so the CRTP base registers through here.
        static void RegisterQueueUpdate(EventsQueueRegistry::UpdateFunction update) {
            EventsQueueRegistry::Register(update, {});
        }
    };

    // CRTP base: every event type has its own channel of listeners and its own queue (see Events/CLAUDE.md).
    template <typename Derived>
    struct EventBase : public BaseEvent {

        template <auto Candidate, typename Type>
        static EventConnection Subscribe(Type* instance, PassKey<SubscriptionHolder>) {
            return GetChannel().Connect(ErasedInstance(instance), &Invoke<Candidate, Type>);
        }

        template <auto Candidate, typename Type>
        static void Unsubscribe(Type* instance) {
            GetChannel().Disconnect(ErasedInstance(instance), &Invoke<Candidate, Type>);
        }

        /**
         * @brief Unsubscribe all handlers for a specific instance
         * @tparam Type Type of the instance
         * @param instance Pointer to the instance
         */
        template <typename Type>
        static void UnsubscribeAll(Type* instance) {
            GetChannel().DisconnectInstance(ErasedInstance(instance));
        }

        static void Emit() requires(std::is_default_constructible_v<Derived>) {
            const Derived event{};
            GetChannel().Publish(&event);
        }

        template <typename... Args>
        static void Emit(Args&&... args) {
            const Derived event{std::forward<Args>(args)...};
            GetChannel().Publish(&event);
        }

        static void Enqueue() requires(std::is_default_constructible_v<Derived>) {
            RegisterOnce();
            GetQueue().push_back(Derived{});
        }

        template <typename... Args>
        static void Enqueue(Args&&... args) {
            RegisterOnce();
            GetQueue().push_back(Derived{std::forward<Args>(args)...});
        }

        /// Delivers the events queued when it started, in order; one that a handler enqueues waits for the next call.
        static void Update() {
            Vector<Derived>& queue = GetQueue();
            const size_t count = queue.size();
            for (size_t i = 0; i < count; ++i) {
                // A local copy of the slot: a handler may enqueue and reallocate the queue.
                Derived event = std::move(queue[i]);
                GetChannel().Publish(&event);
            }
            queue.erase(queue.begin(), queue.begin() + count);
        }

    private:
        // Never freed: a subscriber that outlives static destruction still disconnects from a live channel.
        static EventChannel& GetChannel() {
            static EventChannel* const channel = new EventChannel();
            return *channel;
        }

        static Vector<Derived>& GetQueue() {
            static Vector<Derived> queue;
            return queue;
        }

        template <typename Type>
        static void* ErasedInstance(Type* instance) {
            return const_cast<void*>(static_cast<const void*>(instance));
        }

        // The address of this function is the identity of (Candidate, Type) in the channel. Two instantiations with the same
        // body must not fold into one address (MSVC /OPT:ICF would merge them and Disconnect would hit the wrong handler).
        template <auto Candidate, typename Type>
        static void Invoke(void* instance, const void* event) {
            auto* target = static_cast<Type*>(instance);
            if constexpr (std::is_invocable_v<decltype(Candidate), Type*, const Derived&>) {
                std::invoke(Candidate, target, *static_cast<const Derived*>(event));
            } else {
                std::invoke(Candidate, target);
            }
        }

        static void RegisterOnce() {
            static bool registered = false;
            if (!registered) {
                registered = true;
                RegisterQueueUpdate([]() { Derived::Update(); });
            }
        }
    };

}  // namespace BECore
