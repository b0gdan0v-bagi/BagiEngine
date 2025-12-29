#pragma once

#include <Widgets/IWidget.h>
#include <Core/Math/Color.h>

namespace Core {

    class ClearScreenWidget : public IWidget {
    public:
        ClearScreenWidget() = default;
        ~ClearScreenWidget() override = default;

        bool Initialize(const boost::property_tree::ptree& node) override;
        void Draw() override;
        void Update() override;

    private:
        Math::Color _clearColor{20, 20, 100, 255};
    };

}  // namespace Core

