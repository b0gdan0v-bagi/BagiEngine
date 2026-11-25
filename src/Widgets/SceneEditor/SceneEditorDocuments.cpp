#include "SceneEditorDocuments.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Scene/Scene.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/SceneEditor/UINodeFileSession.h>

namespace BECore {

    namespace {

        struct Ids {
            static constexpr auto sceneConfig = "SceneConfig"_intern;
        };

    }  // namespace

    void AppendSceneDocuments(Vector<EditorDocumentState>& out, const ConfigManager& manager, Scene& scene, bool sceneDirty, const UINodeFileSession& session) {
        out.push_back(MakeConfigDocument(manager, EditorDocumentKind::Scene, scene.GetName().ToStringView(), sceneDirty, Ids::sceneConfig));
        for (const PoolString ref : session.PendingFiles(scene)) {
            out.push_back(MakeNodeFileDocument(manager.GetFileSystem(), ref));
        }
    }

}  // namespace BECore
