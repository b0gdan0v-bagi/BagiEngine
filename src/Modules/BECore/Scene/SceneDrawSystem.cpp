#include "SceneDrawSystem.h"

#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneRenderData.h>

namespace BECore {

    namespace {

        // Copies the fields of the node's TransformComponent / QuadRendererComponent / SpriteRendererComponent into the registry, or
        // removes the registry component when the node has no such component (a no-op when it has none). emplace_or_replace does not
        // allocate once the node has the component.
        void SyncRenderData(entt::registry& registry, SceneNode& node) {
            const entt::entity entity = node.GetEntity();

            if (const auto transform = node.GetComponent<TransformComponent>()) {
                registry.emplace_or_replace<TransformData>(entity, transform->_x, transform->_y, transform->_width, transform->_height);
            } else {
                registry.remove<TransformData>(entity);
            }

            if (const auto quad = node.GetComponent<QuadRendererComponent>()) {
                registry.emplace_or_replace<QuadDrawData>(entity, quad->_color, quad.Get());
            } else {
                registry.remove<QuadDrawData>(entity);
            }

            if (const auto sprite = node.GetComponent<SpriteRendererComponent>()) {
                // Dereferencing the handle updates its access time, as the draw of the sprite does
                const ResourceHandle<ITexture>& texture = sprite->GetTexture();
                registry.emplace_or_replace<SpriteDrawData>(entity, texture ? &*texture : nullptr, sprite->_srcRect, sprite->_fit, sprite.Get());
            } else {
                registry.remove<SpriteDrawData>(entity);
            }
        }

        // Depth-first, a parent before its children: the traversal index is the scene order. emplace_or_replace makes a changed layer
        // show up and does not allocate once the node has its item.
        void WriteDrawItems(entt::registry& registry, SceneNode& node, uint32_t& order) {
            registry.emplace_or_replace<SceneDrawItem>(node.GetEntity(), node.GetLayer(), order++, &node);
            SyncRenderData(registry, node);
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
