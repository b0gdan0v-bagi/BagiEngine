#pragma once

#include <BECore/Widgets/IWidget.h>
#include <Math/Color.h>

namespace BECore {

    class ClearScreenWidget : public IWidget {
        BE_CLASS(ClearScreenWidget)
    public:
        ClearScreenWidget() = default;
        ~ClearScreenWidget() override = default;

        BE_FUNCTION bool Initialize(const XmlNode& node) override;
        BE_FUNCTION void Draw() override;
        BE_FUNCTION void Update() override;

    private:
        Math::Color _clearColor{20, 20, 100, 255};
    };

}  // namespace BECore

