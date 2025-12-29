#include "ClearScreenWidget.h"

#include <Core/Events/RenderEvents.h>
#include <Core/Math/Color.h>

namespace Core {

    bool ClearScreenWidget::Initialize(const boost::property_tree::ptree& node) {
        // Читаем атрибут Color из XML ноды
        auto colorStr = node.get<std::string>("<xmlattr>.Color", "");
        if (!colorStr.empty()) {
            // Используем функцию ParseColorFromString из Math
            _clearColor = Math::Color::ParseColorFromString(colorStr, _clearColor);
        }
        return true;
    }

    void ClearScreenWidget::Draw() {
        // Nothing to draw, clearing is done in Update()
    }

    void ClearScreenWidget::Update() {
        RenderEvents::SetRenderDrawColorEvent::Emit(_clearColor);
        RenderEvents::RenderClearEvent::Emit();
    }

}  // namespace Core

