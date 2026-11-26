#include "SceneControl.h"

#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>

namespace BECore {

    void ReportNodeTouched(ControlService& service, const SceneNode& node, StringView component, StringView field) {
        service.ReportTouch(node.GetRuntimeId(), component, field);
    }

    void ReportNodeTouched(const SceneNode& node, StringView component, StringView field) {
        ReportNodeTouched(CoreManager::GetControlService(), node, component, field);
    }

}  // namespace BECore
