#include "EditorStateControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <BECore/Widgets/EditorStateAnswers.h>
#include <BECore/Widgets/WidgetManager.h>

namespace BECore {

    namespace {

        constexpr StringView kNotEditor = "not_editor";

        struct KindNames {
            static constexpr auto scene = "scene"_intern;
            static constexpr auto nodeFile = "nodeFile"_intern;
            static constexpr auto config = "config"_intern;
            static constexpr auto library = "library"_intern;
        };

        PoolString KindKey(EditorDocumentKind kind) {
            // No default: a new kind is a -Wswitch warning here, not a document that shows as "scene"
            switch (kind) {
                case EditorDocumentKind::Scene:
                    return KindNames::scene;
                case EditorDocumentKind::NodeFile:
                    return KindNames::nodeFile;
                case EditorDocumentKind::Config:
                    return KindNames::config;
                case EditorDocumentKind::Library:
                    return KindNames::library;
            }
            return KindNames::scene;
        }

        void HandleEditorState(ControlCall& call, EditorDocuments& documents) {
            if (CoreManager::GetWidgetManager().GetMode() != RunMode::Editor) {
                call.Fail(kNotEditor, "the editor panels exist only in the Editor run mode");
                return;
            }
            const Vector<EditorDocumentState> states = documents.Collect();
            EditorStateAnswer answer;
            answer.documents.reserve(states.size());
            for (const EditorDocumentState& state : states) {
                EditorStateDocument item;
                item.dirty = state.dirty;
                item.file = TextView(state.file);
                item.id = TextView(state.id);
                item.kind = KindKey(state.kind);
                item.name = TextView(state.name);
                item.path = TextView(state.path);
                answer.anyDirty = answer.anyDirty || state.dirty;
                answer.documents.push_back(item);
            }
            call.ReplyObject(answer);
        }

    }  // namespace

    void RegisterEditorStateControl(ControlService& service, EditorDocuments& documents) {
        service.RegisterHandler("editor_state", [&documents](ControlCall& call) { HandleEditorState(call, documents); });
        service.DescribeMethod("editor_state",
                               {"The documents of the editor panels (scene, node files, selected config, sprite library) with their unsaved flags; fails with not_editor in the Game mode", {}});
    }

}  // namespace BECore
