#include "ClearScreenWidget.h"

#include <Core/Events/RenderEvents.h>
#include <Core/Math/Color.h>

namespace Core {

    bool ClearScreenWidget::Initialize() {
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

