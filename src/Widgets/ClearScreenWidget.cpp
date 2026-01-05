#include "ClearScreenWidget.h"

#include <Events/RenderEvents.h>
#include <Math/Color.h>

namespace Core {

    bool ClearScreenWidget::Initialize(const XmlNode& node) {
        _clearColor = node.ParseAttribute<Math::Color>("Color").value_or(_clearColor);
        return true;
    }

    void ClearScreenWidget::Draw() {
    }

    void ClearScreenWidget::Update() {
        RenderEvents::SetRenderDrawColorEvent::Emit(_clearColor);
        RenderEvents::RenderClearEvent::Emit();
    }

}  // namespace Core

