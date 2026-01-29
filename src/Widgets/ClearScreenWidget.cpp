#include "ClearScreenWidget.h"

#include <BECore/Reflection/IDeserializer.h>
#include <Events/RenderEvents.h>
#include <Math/Color.h>

#include <Generated/ClearScreenWidget.gen.hpp>

namespace BECore {

    bool ClearScreenWidget::Initialize(IDeserializer& deserializer) {
        Deserialize(deserializer);
        return true;
    }

    void ClearScreenWidget::Draw() {
    }

    void ClearScreenWidget::Update() {
        RenderEvents::SetRenderDrawColorEvent::Emit(_clearColor);
        RenderEvents::RenderClearEvent::Emit();
    }

}  // namespace BECore

