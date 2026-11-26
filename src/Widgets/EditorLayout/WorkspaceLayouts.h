#pragma once

#include <Widgets/EditorLayout/Workspaces.h>
#include <imgui.h>

namespace BECore {

    /// Replaces the dock tree of @p dockspaceId, @p size large, with the ready layout of @p workspace and docks its windows (a window it does not dock is
    /// undocked). Call between ImGui::NewFrame and the DockSpace() of the id
    void BuildWorkspaceLayout(ImGuiID dockspaceId, ImVec2 size, EditorWorkspace workspace);

}  // namespace BECore
