#include "SceneDialogsModel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Generated/SceneDialogsModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/IconTable.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>

namespace BECore {

    namespace {

        constexpr StringView kRootChildName = "NewNode";
        constexpr StringView kChildName = "ChildNode";

    }  // namespace

    void SceneDialogsModel::Initialize() {
        Subscribe<SceneAddNodeRequestEvent, &SceneDialogsModel::OnAddNodeRequest>(this);
        Subscribe<SceneAddComponentRequestEvent, &SceneDialogsModel::OnAddComponentRequest>(this);
        BuildComponentRows();
    }

    TypedRef SceneDialogsModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void SceneDialogsModel::BuildComponentRows() {
        _view.components.clear();
        for (const ClassMeta& meta : AbstractFactory<IComponent>::GetInstance().GetRegisteredTypes()) {
            SceneComponentRow& row = _view.components.emplace_back();
            row.typeName.assign(meta.typeName.data(), meta.typeName.size());
            row.label = Format("{} {}", IconOfClass(meta), meta.typeName);
        }
    }

    void SceneDialogsModel::OnAddNodeRequest(const SceneAddNodeRequestEvent& event) {
        Scene* const active = CoreManager::GetSceneManager().GetActiveScene();
        if (active == nullptr || active != event.scene) {
            return;
        }
        SceneNode& root = active->GetRootNode();
        if (event.parent != nullptr && !HierarchyModel::ContainsNode(root, event.parent)) {
            return;
        }
        _scene = active;
        _node = event.parent != nullptr ? event.parent : &root;
        const StringView name = event.parent != nullptr ? kChildName : kRootChildName;
        _view.nodeName.assign(name.data(), name.size());
        _view.canAdd = true;
        _view.addComponentOpen = false;
        _view.addNodeOpen = true;
    }

    void SceneDialogsModel::OnAddComponentRequest(const SceneAddComponentRequestEvent& event) {
        Scene* const active = CoreManager::GetSceneManager().GetActiveScene();
        if (active == nullptr || active != event.scene || event.node == nullptr || !HierarchyModel::ContainsNode(active->GetRootNode(), event.node)) {
            return;
        }
        if (event.node->HasEditorFlag(EditorFlags::Locked)) {
            return;
        }
        _scene = active;
        _node = event.node;
        _view.addNodeOpen = false;
        _view.addComponentOpen = true;
    }

    Scene* SceneDialogsModel::ResolveTarget() {
        Scene* const active = CoreManager::GetSceneManager().GetActiveScene();
        if (active != nullptr && active == _scene && _node != nullptr && HierarchyModel::ContainsNode(active->GetRootNode(), _node)) {
            return active;
        }
        _scene = nullptr;
        _node = nullptr;
        return nullptr;
    }

    void SceneDialogsModel::Update() {
        if (!_view.addNodeOpen && !_view.addComponentOpen) {
            return;
        }
        if (ResolveTarget() == nullptr) {
            Close();
            return;
        }
        _view.canAdd = !_view.nodeName.empty();
    }

    bool SceneDialogsModel::RunCommand(StringView id, StringView argument) {
        using namespace EditorCommandIds;
        if (id == kSceneDialogsAddNode.View()) {
            AddNode();
        } else if (id == kSceneDialogsAddComponent.View()) {
            AddComponent(argument);
        } else if (id == kSceneDialogsCancel.View()) {
            Close();
        } else {
            return false;
        }
        return true;
    }

    void SceneDialogsModel::AddNode() {
        if (!_view.addNodeOpen || _view.nodeName.empty()) {
            return;
        }
        Scene* const scene = ResolveTarget();
        if (scene == nullptr) {
            Close();
            return;
        }
        SceneNode& parent = *_node;
        // AddChild makes a generated node; this one is authored content, so Save writes it
        const IntrusivePtr<SceneNode> node = parent.AddChild(PoolString::Intern(StringView(_view.nodeName.data(), _view.nodeName.size())));
        node->SetGenerated(false);
        SceneNodeAddedEvent::Emit(scene, &parent, node.Get());
        Close();
    }

    void SceneDialogsModel::AddComponent(StringView typeName) {
        if (!_view.addComponentOpen) {
            return;
        }
        Scene* const scene = ResolveTarget();
        if (scene == nullptr) {
            Close();
            return;
        }
        if (_node->HasEditorFlag(EditorFlags::Locked)) {
            Close();
            return;
        }
        const AbstractFactory<IComponent>& factory = AbstractFactory<IComponent>::GetInstance();
        const ClassMeta* const meta = factory.FindMeta(typeName);
        if (meta == nullptr) {
            return;
        }
        IntrusivePtr<IComponent> component = factory.Create(*meta);
        if (!component) {
            return;
        }
        _node->AddComponent(std::move(component));
        SceneEditedEvent::Emit(scene);
        Close();
    }

    void SceneDialogsModel::Close() {
        _view.addNodeOpen = false;
        _view.addComponentOpen = false;
    }

}  // namespace BECore
