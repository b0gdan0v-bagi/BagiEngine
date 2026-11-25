#include "SceneEditorDocuments.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/Scene/Scene.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/SceneEditor/UINodeFileSession.h>

namespace BECore {

    void AppendSceneDocuments(Vector<EditorDocumentState>& out, const ConfigManager& manager, Scene& scene, bool sceneDirty, const UINodeFileSession& session) {
        out.push_back(MakeConfigDocument(manager, EditorDocumentKind::Scene, scene.GetName().ToStringView(), sceneDirty, ConfigNames::scene));
        for (const PoolString ref : session.PendingFiles(scene)) {
            out.push_back(MakeNodeFileDocument(manager.GetFileSystem(), ref));
        }
    }

}  // namespace BECore
