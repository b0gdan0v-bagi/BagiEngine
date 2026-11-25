#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/SceneEditor/SceneDialogEvents.h>

namespace BECore {

    class Scene;
    class SceneNode;

    /// A row of the Add Component list: the text shown (icon and class name) and the class name the command takes
    struct SceneComponentRow {
        BE_CLASS(SceneComponentRow)

        BE_REFLECT_FIELD String label;
        BE_REFLECT_FIELD String typeName;
    };

    /**
     * @brief What `SceneDialogsPanel.xml` binds to: the Add Node and Add Component dialogs
     *
     * The name input writes `nodeName` at every change; the model reads it in its next `Update`. Every other field is the model's.
     */
    struct SceneDialogsView {
        BE_CLASS(SceneDialogsView)

        /// The `open` of the Add Node popup
        BE_REFLECT_FIELD bool addNodeOpen = false;
        BE_REFLECT_FIELD String nodeName;
        /// The name is not empty
        BE_REFLECT_FIELD bool canAdd = false;
        /// The `open` of the Add Component popup
        BE_REFLECT_FIELD bool addComponentOpen = false;
        /// Every registered component class, made once
        BE_REFLECT_FIELD Vector<SceneComponentRow> components;
    };

    /**
     * @brief The model of the Scene Editor's Add Node and Add Component dialogs (an asset of the window kind `Dialogs`)
     *
     * `SceneAddNodeRequestEvent` and `SceneAddComponentRequestEvent` open them. The model keeps the scene and the node as raw pointers and, every frame a dialog
     * is open, closes it when the scene is no longer the active one or the node is no longer in its tree (`HierarchyModel::ContainsNode`, which compares pointers
     * only); a command checks the same before it touches the node. Add Node adds an authored child (`SetGenerated(false)`: Save writes it) and reports it with
     * `SceneNodeAddedEvent`; Add Component adds a new component and reports the scene with `SceneEditedEvent`. A locked node gets no component.
     *
     * Commands (`EditorCommandIds`): `sceneDialogs.addNode`, `sceneDialogs.addComponent` (the class name is the argument) and `sceneDialogs.cancel`.
     */
    class SceneDialogsModel : public IPanelModel, public SubscriptionHolder {
        BE_CLASS(SceneDialogsModel)
    public:
        void Initialize() override;
        [[nodiscard]] TypedRef GetObject() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] const SceneDialogsView& GetView() const {
            return _view;
        }
        [[nodiscard]] SceneDialogsView& GetView() {
            return _view;
        }

    private:
        void OnAddNodeRequest(const SceneAddNodeRequestEvent& event);
        void OnAddComponentRequest(const SceneAddComponentRequestEvent& event);
        void BuildComponentRows();
        /// The scene when it is the active one and holds the node, else null (and the target is forgotten)
        [[nodiscard]] Scene* ResolveTarget();
        void AddNode();
        void AddComponent(StringView typeName);
        void Close();

        SceneDialogsView _view;
        Scene* _scene = nullptr;     // never dereferenced before ResolveTarget has found it to be the active scene
        SceneNode* _node = nullptr;  // the parent of Add Node (the root when the request named none), the target of Add Component
    };

}  // namespace BECore
