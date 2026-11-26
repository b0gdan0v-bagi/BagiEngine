#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/RemoteEditor/RemoteLinkStatus.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>
#include <Widgets/SceneEditor/ISceneEditorHost.h>
#include <cstdint>

namespace BECore {

    class RemoteSession;

    /**
     * @brief What the Scene Editor shows in remote mode: the tree of the attached program, the inspected node and its components, all from the session
     *
     * Nothing here reads or changes a scene of this process. Every state of the link but `Attached` is drawn disabled, over what was read.
     */
    class RemoteSceneView {
    public:
        /// Forgets what the view kept of an earlier link: the search, the filter, the selection it told of
        void Reset();

        /// Lets the typed texts be sent when no field is being edited. Once a frame, also while the window is closed. Needs a frame in progress
        static void ReleaseTypedText(RemoteSession& session);

        /**
         * @brief The content of the Scene Editor's window: the toolbar bar, the link's line, the hierarchy and the inspector. Needs a frame in progress
         *
         * A change of the selection sends `SceneNodeSelectedEvent` with no node, so that the bars ask their host again in the same frame.
         */
        void Draw(RemoteSession& session, StringView toolbarAsset, StringView inspectorAsset);

        /// The component list of the selected node, with no remove button; nothing without a selection
        static void DrawComponents(RemoteSession& session);

        /// What `ISceneEditorHost::GetInspected` answers in remote mode: only the layer takes an edit, and only while `Attached`. Never unsaved
        [[nodiscard]] static SceneInspectedNode Inspect(RemoteSession& session);

        /// Edits the layer of the selected node while `Attached`
        static void SetLayer(RemoteSession& session, int32_t layer);

    private:
        void DrawLink(const RemoteSession& session);
        void AnnounceSelection(const RemoteSession& session);

        HierarchyState _hierarchy;
        RemoteLinkStatus _link;
        /// The selection last told by AnnounceSelection()
        Optional<String> _announced;
    };

}  // namespace BECore
