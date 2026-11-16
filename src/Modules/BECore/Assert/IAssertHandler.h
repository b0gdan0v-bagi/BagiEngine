#pragma once

#include <BECore/Assert/AssertEvent.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Events/SubscriptionHolder.h>

namespace BECore {

    class IAssertHandler : public RefCountedAtomic, public SubscriptionHolder {
        BE_CLASS(IAssertHandler, FACTORY_BASE)

    public:
        ~IAssertHandler() override = default;

        /// Connects the handler to AssertEvent. Called when the entry is deserialized and again by AssertHandlerManager::Initialize, which fixes the order of
        /// delivery by disconnecting every handler and connecting them again in order: an override calls Disconnect() and connects on every call.
        virtual void Initialize() = 0;

        /// Drops every connection of the handler under the assert lock (a worker may emit meanwhile). Not virtual: whatever Initialize() subscribed is held by the
        /// SubscriptionHolder.
        void Disconnect() {
            AssertEvent::UnsubscribeAll(this);
            UnsubscribeAll();
        }

        /// Runs on the thread that failed the assert, one handler call at a time across all threads (AssertEvent holds a lock), so state that only the handlers touch
        /// needs no lock of its own. It may fail an assert itself. Keep it short: other threads that fail an assert wait for it.
        virtual void OnAssert(const AssertEvent& event) = 0;

        /// Lower runs first; handlers of equal priority run in config order.
        virtual int GetPriority() const {
            return _priority;
        }

        void SetPriority(int priority) {
            _priority = priority;
        }

    protected:
        IAssertHandler() = default;

        BE_REFLECT_FIELD int _priority = 0;
    };

}  // namespace BECore
