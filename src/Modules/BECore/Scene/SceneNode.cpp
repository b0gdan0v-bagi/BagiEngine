#include "SceneNode.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Scene.h>
#include <Generated/SceneNode.gen.hpp>

namespace BECore {

    SceneNode* SceneNode::GetParent() const {
        return _parent;
    }

    bool SceneNode::IsDescendantOf(const SceneNode& ancestor) const {
        for (const SceneNode* node = _parent; node != nullptr; node = node->_parent) {
            if (node == &ancestor) {
                return true;
            }
        }
        return false;
    }

    const eastl::vector<IntrusivePtr<SceneNode>>& SceneNode::GetChildren() const {
        return _children;
    }

    IntrusivePtr<SceneNode> SceneNode::AddChild(PoolString name) {
        ASSERT(_scene != nullptr);
        auto child = _scene->CreateNode(name, this);
        return child;
    }

    void SceneNode::RemoveChild(SceneNode* child) {
        if (!child) {
            return;
        }

        // From the tail: a field of nodes that shrinks removes the last child, and that is then no walk of the whole list
        const auto reverseIt = eastl::find_if(_children.rbegin(), _children.rend(), [child](const IntrusivePtr<SceneNode>& node) { return node.Get() == child; });

        if (reverseIt != _children.rend()) {
            const auto it = _children.begin() + (_children.rend() - reverseIt - 1);
            // The registry must not keep a SceneDrawItem that points into the removed subtree
            if (_scene) {
                _scene->DestroyNodeEntities(**it, {});
            }
            (*it)->_parent = nullptr;
            _children.erase(it);
        }
    }

    void SceneNode::AddComponent(IntrusivePtr<IComponent> component) {
        if (!component) {
            return;
        }

        auto typeName = PoolString::Intern(component->GetTypeMeta().typeName);
        RemoveComponent(typeName);

        component->_scene = _scene;
        component->_node = this;
        _components.push_back(component);
        component->Initialize();
        component->OnAttached();
    }

    void SceneNode::RemoveComponent(PoolString typeName) {
        auto it = eastl::find_if(_components.begin(), _components.end(), [&typeName](const IntrusivePtr<IComponent>& comp) { return PoolString::Intern(comp->GetTypeMeta().typeName) == typeName; });

        if (it != _components.end()) {
            _components.erase(it);
        }
    }

    IntrusivePtr<IComponent> SceneNode::GetComponent(StringView typeName) const {
        auto it = eastl::find_if(_components.begin(), _components.end(), [&typeName](const IntrusivePtr<IComponent>& comp) { return comp->GetTypeMeta().typeName == typeName; });

        if (it != _components.end()) {
            return *it;
        }
        return {};
    }

    void SceneNode::WireToScene(Scene* scene, SceneNode* parent) {
        _scene = scene;
        _parent = parent;
        _entity = scene->GetRegistry().create();

        for (auto& comp : _components) {
            comp->_scene = scene;
            comp->_node = this;
            comp->OnAttached();
        }

        for (auto& child : _children) {
            child->WireToScene(scene, this);
        }
    }

    PoolString SceneNode::GetName() const {
        return _name;
    }

    void SceneNode::SetName(PoolString name) {
        _name = name;
    }

    int32_t SceneNode::GetLayer() const {
        return _layer;
    }

    void SceneNode::SetLayer(int32_t layer) {
        _layer = layer;
    }

    PoolString SceneNode::GetSourceRef() const {
        return _sourceRef;
    }

    void SceneNode::SetSourceRef(PoolString sourceRef) {
        _sourceRef = sourceRef;
    }

    EditorFlags SceneNode::GetEditorFlags() const {
        return _editorFlags;
    }

    void SceneNode::SetEditorFlags(EditorFlags flags) {
        _editorFlags = flags;
    }

    bool SceneNode::HasEditorFlag(EditorFlags flag) const {
        return (_editorFlags & flag) != EditorFlags::None;
    }

    entt::entity SceneNode::GetEntity() const {
        return _entity;
    }

    const eastl::vector<IntrusivePtr<IComponent>>& SceneNode::GetComponents() const {
        return _components;
    }

}  // namespace BECore
