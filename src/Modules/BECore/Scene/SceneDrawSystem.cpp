#include "SceneDrawSystem.h"

#include <BECore/Scene/Scene.h>

namespace BECore {

    namespace {

        // Depth-first, a parent before its children: the traversal index is the scene order. emplace_or_replace makes a changed layer
        // show up and does not allocate once the node has its item.
        void WriteDrawItems(entt::registry& registry, SceneNode& node, uint32_t& order) {
            registry.emplace_or_replace<SceneDrawItem>(node.GetEntity(), node.GetLayer(), order++, &node);
            for (const auto& child : node.GetChildren()) {
                WriteDrawItems(registry, *child, order);
            }
        }

    }  // namespace

    void SceneDrawSystem::Run(Scene& scene) {
        entt::registry& registry = scene.GetRegistry();

        uint32_t order = 0;
        WriteDrawItems(registry, scene.GetRootNode(), order);

        // The keys are unique (order is the traversal index), so the result does not depend on the sort being stable.
        // EnTT sorts so that a view iterates in the order of the comparator: the first item of the view is the lowest (layer, order).
        registry.sort<SceneDrawItem>([](const SceneDrawItem& a, const SceneDrawItem& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });

        for (const auto [entity, item] : registry.view<SceneDrawItem>().each()) {
            for (const auto& component : item.node->GetComponents()) {
                component->OnDraw();
            }
        }
    }

}  // namespace BECore
