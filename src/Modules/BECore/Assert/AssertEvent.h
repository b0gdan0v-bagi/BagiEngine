#pragma once

#include <Events/EventBase.h>
#include <mutex>

namespace BECore {

    enum class AssertType { Assert, Expect, FatalError };

    /**
     * @brief A failed ASSERT, EXPECT or FATALERROR. Any thread may emit it.
     *
     * Emit, Subscribe, Unsubscribe and UnsubscribeAll take one recursive lock of their own, so the handlers run on the emitting thread one at a time (a handler
     * may fail an assert itself) and a handler connected while a worker emits is not seen half-connected. Keep a handler short: every other thread that fails
     * an assert waits for it. The channel itself stays "one thread only" (Events/CLAUDE.md), so everything that touches it goes through the functions here.
     *
     * A connection that a SubscriptionHolder drops (its destructor, SubscriptionHolder::UnsubscribeAll) disconnects without this lock. That is safe for a handler that
     * lives from start-up to shutdown, because the task pool is joined before the managers go; a handler that is removed while a worker may still emit calls
     * AssertEvent::UnsubscribeAll(handler) first, which leaves nothing for the connection to remove.
     */
    struct AssertEvent : EventBase<AssertEvent> {
        const char* file;
        int line;
        const char* expression;  ///< nullptr for FATALERROR
        const char* message;     ///< nullptr when the macro had none
        AssertType type;

        AssertEvent(const char* file, int line, const char* expression, const char* message, AssertType type) : file(file), line(line), expression(expression), message(message), type(type) {}

        static void Emit(const char* file, int line, const char* expression, const char* message, AssertType type) {
            const Guard guard;
            EventBase<AssertEvent>::Emit(file, line, expression, message, type);
        }

        template <auto Candidate, typename Type>
        static EventConnection Subscribe(Type* instance, PassKey<SubscriptionHolder> key) {
            const Guard guard;
            return EventBase<AssertEvent>::template Subscribe<Candidate>(instance, std::move(key));
        }

        template <auto Candidate, typename Type>
        static void Unsubscribe(Type* instance) {
            const Guard guard;
            EventBase<AssertEvent>::template Unsubscribe<Candidate>(instance);
        }

        template <typename Type>
        static void UnsubscribeAll(Type* instance) {
            const Guard guard;
            EventBase<AssertEvent>::UnsubscribeAll(instance);
        }

    private:
        /// Holds the assert lock for a scope. The lock is a raw mutex on purpose: a BECore::Mutex asserts in its lock-order check, and an assert inside the
        /// lock code would come back here.
        class Guard {
        public:
            Guard() {
                GetMutex().lock();
            }
            ~Guard() {
                GetMutex().unlock();
            }
            Guard(const Guard&) = delete;
            Guard& operator=(const Guard&) = delete;
        };

        // Never freed, like the channels: an assert during static destruction still finds a live lock. Defined here because EventsModule and BECoreModule
        // use the macros without linking each other. Raw because an engine Mutex asserts in LockOrder, so an assert inside the lock code would recurse.
        static std::recursive_mutex& GetMutex() {                                   //-NoStd: the assert lock must not assert itself (Guard)
            static std::recursive_mutex* const mutex = new std::recursive_mutex();  //-NoStd: the assert lock must not assert itself (Guard)
            return *mutex;
        }
    };

}  // namespace BECore
