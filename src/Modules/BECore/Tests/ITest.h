#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>

namespace BECore::Tests {

    class ITest : public RefCounted {
        BE_CLASS(ITest, FACTORY_BASE)
    public:
        ITest() = default;
        ~ITest() override = default;

        virtual bool Run() = 0;
    };

}  // namespace BECore::Tests
