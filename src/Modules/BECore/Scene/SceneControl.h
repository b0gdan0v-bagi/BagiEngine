#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;
    class SceneNode;

    /**
     * @brief Says that the control call of @p service running now touched @p node, or a component (its type name) and a field of it.
     *
     * For a method of a game that changes or acts on a node, before its answer: the editor shows the user where the client was. The rules are those of
     * ControlService::ReportTouch, so outside a call and for a call with no client nothing is recorded; a change made after the call takes the
     * overload of ReportTouch() that names the client, with SceneNode::GetRuntimeId().
     */
    void ReportNodeTouched(ControlService& service, const SceneNode& node, StringView component = {}, StringView field = {});

    /// ReportNodeTouched() over the service of CoreManager
    void ReportNodeTouched(const SceneNode& node, StringView component = {}, StringView field = {});

}  // namespace BECore
