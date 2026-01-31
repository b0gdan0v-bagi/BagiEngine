#pragma once

#include <BECore/Assert/AssertEvent.h>
#include <BECore/RefCounted/RefCounted.h>
#include <Events/SubscriptionHolder.h>

namespace BECore {

    class IAssertHandler : public RefCountedAtomic, public SubscriptionHolder {
    public:
        ~IAssertHandler() override = default;

        virtual void Initialize() = 0;

        virtual void OnAssert(const AssertEvent& event) = 0;

        virtual int GetPriority() const {
            return _priority;
        }

        void SetPriority(int priority) {
            _priority = priority;
        }

    protected:
        IAssertHandler() = default;

        int _priority = 0;
    };

}  // namespace BECore
