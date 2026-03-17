#include "SceneNode.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Scene.h>
#include <Generated/SceneNode.gen.hpp>

namespace BECore {

    SceneNode* SceneNode::GetParent() const {
        return _parent;
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

        auto it = eastl::find_if(_children.begin(), _children.end(), [child](const IntrusivePtr<SceneNode>& node) { return node.Get() == child; });

        if (it != _children.end()) {
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
    }

    void SceneNode::RemoveComponent(PoolString typeName) {
        auto it = eastl::find_if(_components.begin(), _components.end(), [&typeName](const IntrusivePtr<IComponent>& comp) { return PoolString::Intern(comp->GetTypeMeta().typeName) == typeName; });

        if (it != _components.end()) {
            _components.erase(it);
        }
    }

    IntrusivePtr<IComponent> SceneNode::GetComponent(eastl::string_view typeName) const {
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
            comp->Initialize();
        }

        for (auto& child : _children) {
            child->WireToScene(scene, this);
        }
    }

    PoolString SceneNode::GetName() const {
        return _name;
    }

    entt::entity SceneNode::GetEntity() const {
        return _entity;
    }

}  // namespace BECore
