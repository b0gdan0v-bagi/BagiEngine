#include "Scene.h"

#include <BECore/GameManager/CoreManager.h>
#include <EASTL/sort.h>
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
        const Time& time = CoreManager::GetTime();
        for (uint32_t step = 0; step < time.GetFixedStepCount(); ++step) {
            SceneEvents::SceneFixedUpdateEvent::Emit(time.GetFixedDeltaTime());
        }
        SceneEvents::SceneUpdateEvent::Emit(time.GetDeltaTime());
    }

    void Scene::Draw() {
        if (!_rootNode) {
            return;
        }

        _drawEntries.clear();
        CollectDrawEntries(*_rootNode);

        // The keys are unique (order is the traversal index), so an unstable sort still gives one result.
        eastl::sort(_drawEntries.begin(), _drawEntries.end(), [](const SceneDrawEntry& a, const SceneDrawEntry& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });

        for (const SceneDrawEntry& entry : _drawEntries) {
            for (const auto& component : entry.node->GetComponents()) {
                component->OnDraw();
            }
        }
    }

    void Scene::CollectDrawEntries(SceneNode& node) {
        _drawEntries.push_back({node.GetLayer(), static_cast<uint32_t>(_drawEntries.size()), &node});
        for (const auto& child : node.GetChildren()) {
            CollectDrawEntries(*child);
        }
    }

    bool Scene::IsActive() const {
        return _active;
    }

    void Scene::SetActive(bool active, PassKey<SceneManager>) {
        _active = active;
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

    void Scene::PrepareForSave() {
        if (_rootNode) {
            _nodes = _rootNode->_children;
        }
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
