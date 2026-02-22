#pragma once

#include <BECore/Assert/AssertEvent.h>
#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    class IAssertHandler : public RefCountedAtomic, public SubscriptionHolder {
        BE_CLASS(IAssertHandler, FACTORY_BASE)

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

        BE_REFLECT_FIELD int _priority = 0;
    };

}  // namespace BECore
