#pragma once

#include <BECore/Widgets/IWidget.h>
#include <BECore/Math/Color.h>

namespace BECore {

    class IDeserializer;

    class ClearScreenWidget : public IWidget {
        BE_CLASS(ClearScreenWidget)
    public:
        ClearScreenWidget() = default;
        ~ClearScreenWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Draw() override;
        BE_FUNCTION void Update() override;

    private:
        BE_REFLECT_FIELD BECore::Color _clearColor{20, 20, 100, 255};
    };

}  // namespace BECore

