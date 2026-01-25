#include "ClearScreenWidget.h"

#include <BECore/Reflection/IArchive.h>
#include <Events/RenderEvents.h>
#include <Math/Color.h>

#include <Generated/ClearScreenWidget.gen.hpp>

namespace BECore {

    bool ClearScreenWidget::Initialize(IArchive& archive) {
        // Color is a nested object with attributes for r, g, b, a
        if (archive.BeginObject("Color")) {
            _clearColor.Serialize(archive);
            archive.EndObject();
        }
        return true;
    }

    void ClearScreenWidget::Draw() {
    }

    void ClearScreenWidget::Update() {
        RenderEvents::SetRenderDrawColorEvent::Emit(_clearColor);
        RenderEvents::RenderClearEvent::Emit();
    }

}  // namespace BECore

