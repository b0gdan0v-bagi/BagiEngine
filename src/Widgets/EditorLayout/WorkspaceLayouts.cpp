#include "WorkspaceLayouts.h"

#include <imgui_internal.h>

namespace BECore {

    namespace {

        bool HasDock(const ReadyWorkspace& ready, WorkspaceSlot slot) {
            for (const WorkspaceDock& dock : ready.docks) {
                if (dock.slot == slot) {
                    return true;
                }
            }
            return false;
        }

        /// The name the window of @p panel is docked by, null for a panel the table lacks
        const char* WindowOf(PoolString panel) {
            for (const WorkspacePanel& entry : GetWorkspacePanels()) {
                if (entry.id == panel) {
                    return entry.window.data();
                }
            }
            return nullptr;
        }

    }  // namespace

    void BuildWorkspaceLayout(ImGuiID dockspaceId, ImVec2 size, EditorWorkspace workspace) {
        const ReadyWorkspace& ready = GetReadyWorkspace(workspace);
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, size);

        Array<ImGuiID, EnumUtils<WorkspaceSlot>::Count()> nodes{};
        const auto node = [&nodes](WorkspaceSlot slot) -> ImGuiID& { return nodes[static_cast<size_t>(slot)]; };
        // Splits @p ratio of the node of @p from off towards @p direction into the node of @p into; the rest stays the node of @p from
        const auto split = [&node](WorkspaceSlot from, ImGuiDir direction, float ratio, WorkspaceSlot into) {
            const ImGuiID parent = node(from);
            ImGui::DockBuilderSplitNode(parent, direction, ratio, &node(into), &node(from));
        };

        // The right column spans the whole height, so it is split off first
        node(WorkspaceSlot::Center) = dockspaceId;
        const bool right = HasDock(ready, WorkspaceSlot::Right) || HasDock(ready, WorkspaceSlot::RightBottom);
        if (right) {
            split(WorkspaceSlot::Center, ImGuiDir_Right, ready.rightWidth, WorkspaceSlot::Right);
        }
        if (HasDock(ready, WorkspaceSlot::Left) || HasDock(ready, WorkspaceSlot::LeftBottom)) {
            split(WorkspaceSlot::Center, ImGuiDir_Left, ready.leftWidth, WorkspaceSlot::Left);
        }
        if (HasDock(ready, WorkspaceSlot::LeftBottom)) {
            split(WorkspaceSlot::Left, ImGuiDir_Down, ready.leftBottomHeight, WorkspaceSlot::LeftBottom);
        }
        if (HasDock(ready, WorkspaceSlot::CenterBottom)) {
            split(WorkspaceSlot::Center, ImGuiDir_Down, ready.centerBottomHeight, WorkspaceSlot::CenterBottom);
        }
        if (HasDock(ready, WorkspaceSlot::RightBottom)) {
            split(WorkspaceSlot::Right, ImGuiDir_Down, ready.rightBottomHeight, WorkspaceSlot::RightBottom);
        }

        for (const WorkspaceDock& dock : ready.docks) {
            if (const char* window = WindowOf(dock.panel)) {
                ImGui::DockBuilderDockWindow(window, node(dock.slot));
            }
        }
        ImGui::DockBuilderFinish(dockspaceId);
    }

}  // namespace BECore
