#include "Scene.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/SceneDrawSystem.h>
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
        if (_viewVersion != _announcedViewVersion) {
            _announcedViewVersion = _viewVersion;
            SceneEvents::SceneViewChangedEvent::Emit(this, GetActiveView());
        }
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

        // The view and its fit follow the output size of every frame, so a resized window or viewport needs no event. The pick comes first:
        // everything drawn below (and every canvas that lays out) reads the active view.
        IRenderer* renderer = CoreManager::GetRenderer().Get();
        PickView(renderer ? renderer->GetOutputWidth() : 0.0f, renderer ? renderer->GetOutputHeight() : 0.0f);
        const ActiveSceneView view = GetActiveView();
        _viewTransform = renderer ? ViewTransform::Fit(view.width, view.height, renderer->GetOutputWidth(), renderer->GetOutputHeight()) : ViewTransform{};
        if (renderer) {
            renderer->SetViewTransform(_viewTransform);
        }

        SceneDrawSystem::Run(*this);

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

    void Scene::PickView(float outputWidth, float outputHeight) {
        int32_t index = 0;
        if (_viewOverride && *_viewOverride >= -1 && *_viewOverride < static_cast<int32_t>(_viewVariants.size())) {
            index = *_viewOverride;
        } else {
            index = SceneViewSelector::Pick(_viewWidth, _viewHeight, _viewVariants, outputWidth, outputHeight).index;
        }
        if (index != _activeViewIndex) {
            _activeViewIndex = index;
            ++_viewVersion;
        }
    }

    ActiveSceneView Scene::GetActiveView() const {
        if (_activeViewIndex >= 0 && static_cast<size_t>(_activeViewIndex) < _viewVariants.size()) {
            const SceneViewVariant& variant = _viewVariants[static_cast<size_t>(_activeViewIndex)];
            return {variant.name, variant.width, variant.height, _activeViewIndex};
        }
        return {PoolString{}, _viewWidth, _viewHeight, -1};
    }

    uint32_t Scene::GetViewVersion() const {
        return _viewVersion;
    }

    eastl::span<const SceneViewVariant> Scene::GetViewVariants() const {
        return {_viewVariants.data(), _viewVariants.size()};
    }

    void Scene::SetViewOverride(std::optional<int32_t> index) {
        _viewOverride = index;
    }

    float Scene::GetViewWidth() const {
        return GetActiveView().width;
    }

    float Scene::GetViewHeight() const {
        return GetActiveView().height;
    }

    float Scene::ScreenToSceneX(float outputX) const {
        return _viewTransform.ToViewX(outputX);
    }

    float Scene::ScreenToSceneY(float outputY) const {
        return _viewTransform.ToViewY(outputY);
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

    void Scene::DestroyNodeEntities(SceneNode& node, PassKey<SceneNode>) {
        DestroyEntities(node);
    }

    void Scene::DestroyEntities(SceneNode& node) {
        for (const auto& child : node.GetChildren()) {
            DestroyEntities(*child);
        }
        if (_registry.valid(node._entity)) {
            _registry.destroy(node._entity);
        }
        node._entity = entt::null;
    }

}  // namespace BECore
