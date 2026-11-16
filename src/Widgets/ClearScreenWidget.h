#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;

    class ClearScreenWidget : public IWidget {
        BE_CLASS(ClearScreenWidget)
    public:
        ClearScreenWidget() = default;
        ~ClearScreenWidget() override = default;

        bool Initialize(IDeserializer& deserializer) override;
        void Draw() override;
        void Update() override;

    private:
        BE_REFLECT_FIELD BECore::Color _clearColor{20, 20, 100, 255};
    };

}  // namespace BECore
