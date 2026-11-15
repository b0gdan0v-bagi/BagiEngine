#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Events/SubscriptionHolder.h>

namespace BECore {

    class IDeserializer;

    class IWidget : public RefCounted, public SubscriptionHolder {
        BE_CLASS(IWidget, FACTORY_BASE)
    public:
        IWidget() = default;
        ~IWidget() override = default;

        virtual bool Initialize(IDeserializer& deserializer) = 0;

        virtual void Update() = 0;
        virtual void DrawScreen() {}
        virtual void Draw() = 0;
    };

}  // namespace BECore
