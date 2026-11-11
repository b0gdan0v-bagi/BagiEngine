#include "ClearScreenWidget.h"

#include <BECore/Reflection/IDeserializer.h>
#include <Events/RenderEvents.h>
#include <Generated/ClearScreenWidget.gen.hpp>

namespace BECore {

    bool ClearScreenWidget::Initialize(IDeserializer& /*deserializer*/) {
        // _clearColor was read by the loader before this call
        return true;
    }

    void ClearScreenWidget::Draw() {}

    void ClearScreenWidget::Update() {
        BE_PROFILE_SCOPE("ClearScreenWidget::Update");
        RenderEvents::SetRenderDrawColorEvent::Emit(_clearColor);
        RenderEvents::RenderClearEvent::Emit();
    }

}  // namespace BECore
