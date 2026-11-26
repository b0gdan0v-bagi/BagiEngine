#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Workspaces.h>
#include <imgui.h>

namespace BECore {

    /**
     * @brief Puts the workspaces on screen: the dock layout of ImGui and the open flags of the panels in EditorPanelRegistry
     *
     * The host (EditorLayoutWidget) calls ApplyPending before every ImGui::NewFrame, Update in every frame before its DockSpace(), and Shutdown at its death.
     * SwitchTo, Reset and LoadLayout may come at any time of a frame; the next ApplyPending carries them out, because an ini cannot be loaded in the middle of
     * a frame, and a ready layout is built by the Update after it.
     *
     * The open flags are read in Update and not at the death: the panels made after the host are destroyed before it.
     */
    class WorkspaceSwitcher {
    public:
        /// Reads `workspaces.json` of @p userDirectory; empty keeps everything in memory
        explicit WorkspaceSwitcher(Path userDirectory);

        [[nodiscard]] EditorWorkspace GetActive() const {
            return _model.GetActive();
        }

        /// Stores what is on screen into the workspace left and shows @p target; nothing for the active one
        void SwitchTo(EditorWorkspace target);

        /// Back to the ready layout and panels of the active workspace
        void Reset();

        /// Shows the layout text @p text, which has a dock tree, and keeps it as the active workspace's
        void LoadLayout(String text);

        /// Before ImGui::NewFrame
        void ApplyPending();

        /**
         * @brief In a frame, before the DockSpace() of @p dockspaceId
         *
         * The first call gives the panels the stored flags and keeps the dock tree ImGui read from its ini; without one it shows the active workspace.
         */
        void Update(ImGuiID dockspaceId, ImVec2 size);

        /// Stores the layout on screen, while the ImGui context is alive; nothing before the first Update or while a change is not on screen yet
        void Shutdown();

    private:
        void Start(ImGuiID dockspaceId, ImVec2 size);
        void SetPending(WorkspaceState state);
        void NoteOpenFlags();

        Workspaces _model;
        Optional<WorkspaceState> _pending;  // waits for ApplyPending
        bool _started = false;
        bool _buildReady = false;  // the applied state has no layout text: the next Update builds the ready one
        bool _checkTree = false;   // a loaded text may hold no tree for the dock space
    };

}  // namespace BECore
