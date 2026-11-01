#include "Scene.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
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

        // The fit follows the output size of every frame, so a resized window or viewport needs no event.
        IRenderer* renderer = CoreManager::GetRenderer().Get();
        _viewTransform = renderer ? ViewTransform::Fit(_viewWidth, _viewHeight, renderer->GetOutputWidth(), renderer->GetOutputHeight()) : ViewTransform{};
        if (renderer) {
            renderer->SetViewTransform(_viewTransform);
        }

        for (const SceneDrawEntry& entry : _drawEntries) {
            for (const auto& component : entry.node->GetComponents()) {
                component->OnDraw();
            }
        }

        // Widgets and ImGui draw in output pixels, so the scene's fit must not leak out of its pass.
        if (renderer) {
            renderer->SetViewTransform({});
        }
    }

    namespace {

        // A node is drawn before the target when its layer is lower, or when it has the target's layer and the walk (scene order) reaches it first
        struct RankWalk {
            const SceneNode* target = nullptr;
            int32_t targetLayer = 0;
            int32_t rank = 0;
            bool found = false;

            void Visit(const SceneNode& node) {
                if (&node == target) {
                    found = true;
                } else if (node.GetLayer() < targetLayer || (node.GetLayer() == targetLayer && !found)) {
                    ++rank;
                }
                for (const auto& child : node.GetChildren()) {
                    Visit(*child);
                }
            }
        };

    }  // namespace

    int32_t Scene::GetDrawRank(const SceneNode& node) const {
        if (!_rootNode) {
            return 0;
        }
        RankWalk walk;
        walk.target = &node;
        walk.targetLayer = node.GetLayer();
        walk.Visit(*_rootNode);
        return walk.found ? walk.rank : 0;
    }

    float Scene::GetViewWidth() const {
        return _viewWidth;
    }

    float Scene::GetViewHeight() const {
        return _viewHeight;
    }

    float Scene::ScreenToSceneX(float outputX) const {
        return _viewTransform.ToViewX(outputX);
    }

    float Scene::ScreenToSceneY(float outputY) const {
        return _viewTransform.ToViewY(outputY);
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
