#include "Scene.h"

#include <Events/SceneEvents.h>
#include <Generated/Scene.gen.hpp>

namespace BECore {

    void Scene::Initialize() {
        _rootNode = BECore::New<SceneNode>();
        _rootNode->_name = "Root"_intern;
        _rootNode->_scene = this;
        _rootNode->_entity = _registry.create();

        for (auto& node : _nodes) {
            node->WireToScene(this, _rootNode.Get());
            _rootNode->_children.push_back(node);
        }
    }

    void Scene::Update() {
        SceneEvents::SceneUpdateEvent::Emit();
    }

    void Scene::Draw() {
        SceneEvents::SceneDrawEvent::Emit();
    }

    entt::registry& Scene::GetRegistry() {
        return _registry;
    }

    SceneNode& Scene::GetRootNode() {
        return *_rootNode;
    }

    PoolString Scene::GetName() const {
        return _name;
    }

    IntrusivePtr<SceneNode> Scene::CreateNode(PoolString name, SceneNode* parent) {
        auto node = BECore::New<SceneNode>();
        node->_name = name;
        node->_entity = _registry.create();
        node->_scene = this;

        SceneNode* targetParent = parent ? parent : _rootNode.Get();
        if (targetParent) {
            node->_parent = targetParent;
            targetParent->_children.push_back(node);
        }

        return node;
    }

}  // namespace BECore
