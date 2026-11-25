#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Widgets/EditorDocuments.h>

namespace BECore {

    class ConfigManager;
    class Scene;
    class UINodeFileSession;

    /**
     * @brief Appends the documents of the Scene Editor: the scene (`SceneConfig`, dirty as the Save button says) and one node file per file the pending UI edits would patch
     *
     * No ImGui, and a panel that is closed reports too: the flags are data, not drawn state.
     */
    void AppendSceneDocuments(Vector<EditorDocumentState>& out, const ConfigManager& manager, Scene& scene, bool sceneDirty, const UINodeFileSession& session);

}  // namespace BECore
