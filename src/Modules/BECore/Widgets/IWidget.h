#pragma once
#include <BECore/Reflection/ReflectionMarkers.h>

namespace BECore {

    class IDeserializer;

    class IWidget : public RefCounted, public SubscriptionHolder {
        BE_CLASS(IWidget, FACTORY_BASE)
    public:
        IWidget() = default;
        ~IWidget() override = default;

        BE_FUNCTION virtual bool Initialize(IDeserializer& deserializer) = 0;

        BE_FUNCTION virtual void Update() = 0;
        BE_FUNCTION virtual void Draw() = 0;
    };

}  // namespace BECore

