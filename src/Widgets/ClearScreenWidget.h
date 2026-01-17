#pragma once

#include <BECore/Widgets/IWidget.h>
#include <Math/Color.h>

namespace BECore {

    class ClearScreenWidget : public IWidget {
    public:
        ClearScreenWidget() = default;
        ~ClearScreenWidget() override = default;

        bool Initialize(const XmlNode& node) override;
        void Draw() override;
        void Update() override;

    private:
        Math::Color _clearColor{20, 20, 100, 255};
    };

}  // namespace BECore

