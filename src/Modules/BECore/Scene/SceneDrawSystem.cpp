#include "SceneDrawSystem.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Renderer/DrawTransform.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneRenderData.h>

namespace BECore {

    namespace {

        constexpr Color kNoTint{255, 255, 255, 255};  // a sprite is drawn as it is: white leaves the texel as it is

        // Copies the fields of the node's TransformComponent / QuadRendererComponent / SpriteRendererComponent into the registry, or
        // removes the registry component when the node has no such component (a no-op when it has none). emplace_or_replace does not
        // allocate once the node has the component. One pass over the components; the first of each type counts, as GetComponent<T>().
        void SyncRenderData(entt::registry& registry, SceneNode& node) {
            const entt::entity entity = node.GetEntity();

            const TransformComponent* transform = nullptr;
            const QuadRendererComponent* quad = nullptr;
            const SpriteRendererComponent* sprite = nullptr;
            for (const auto& component : node.GetComponents()) {
                const StringView typeName = component->GetTypeMeta().typeName;
                if (!transform && typeName == TransformComponent::GetStaticTypeName()) {
                    transform = static_cast<const TransformComponent*>(component.Get());
                } else if (!quad && typeName == QuadRendererComponent::GetStaticTypeName()) {
                    quad = static_cast<const QuadRendererComponent*>(component.Get());
                } else if (!sprite && typeName == SpriteRendererComponent::GetStaticTypeName()) {
                    sprite = static_cast<const SpriteRendererComponent*>(component.Get());
                }
            }

            if (transform) {
                registry.emplace_or_replace<TransformData>(entity, transform->_x, transform->_y, transform->_width, transform->_height, transform->_rotation, transform->_pivotX, transform->_pivotY);
            } else {
                registry.remove<TransformData>(entity);
            }

            if (quad) {
                registry.emplace_or_replace<QuadDrawData>(entity, quad->_color, quad->_blend, quad);
            } else {
                registry.remove<QuadDrawData>(entity);
            }

            if (sprite) {
                // Dereferencing the handle marks the texture as used (updates its access time)
                const ResourceHandle<ITexture>& texture = sprite->GetTexture();
                registry.emplace_or_replace<SpriteDrawData>(entity, texture ? &*texture : nullptr, sprite->_srcRect, sprite->_fit, sprite->_blend, sprite->_flipX, sprite->_flipY, sprite);
            } else {
                registry.remove<SpriteDrawData>(entity);
            }
        }

        // The turn of a node: about the pivot of the node's rect (not of the rect a Contain sprite is fitted into), as an absolute point
        // in scene units, which is what DrawTransform wants. flipX / flipY are the sprite's.
        DrawTransform MakeDrawTransform(const TransformData& transform, bool flipX, bool flipY) {
            return DrawTransform{transform.rotation, transform.x + transform.pivotX * transform.width, transform.y + transform.pivotY * transform.height, flipX, flipY};
        }

        // A quad without a transform draws nothing. The blend mode is the quad's alone: the guard puts the previous one back.
        void DrawQuad(const QuadDrawData& quad, const TransformData* transform) {
            if (!transform) {
                return;
            }
            IRenderer& renderer = *CoreManager::GetRenderer();
            const ScopedBlendMode blend(renderer, quad.blend);
            const DrawTransform drawTransform = MakeDrawTransform(*transform, false, false);
            if (drawTransform.IsIdentity()) {
                renderer.DrawFilledRect(transform->x, transform->y, transform->width, transform->height, quad.color);
            } else {
                renderer.DrawTransformedRect(transform->x, transform->y, transform->width, transform->height, quad.color, drawTransform);
            }
        }

        // A sprite without a texture or a transform draws nothing. An empty source rect means the whole texture.
        void DrawSprite(const SpriteDrawData& sprite, const TransformData* transform) {
            if (!sprite.texture || !transform) {
                return;
            }
            const Rect* src = sprite.srcRect.IsEmpty() ? nullptr : &sprite.srcRect;
            const float srcWidth = src ? src->w : sprite.texture->GetWidth();
            const float srcHeight = src ? src->h : sprite.texture->GetHeight();
            const Rect dst = FitSpriteRect(srcWidth, srcHeight, Rect{transform->x, transform->y, transform->width, transform->height}, sprite.fit);
            IRenderer& renderer = *CoreManager::GetRenderer();
            const ScopedBlendMode blend(renderer, sprite.blend);
            const DrawTransform drawTransform = MakeDrawTransform(*transform, sprite.flipX, sprite.flipY);
            if (drawTransform.IsIdentity()) {
                renderer.DrawTexture(*sprite.texture, src, dst.x, dst.y, dst.w, dst.h);
            } else {
                renderer.DrawTransformedTexture(*sprite.texture, src, dst.x, dst.y, dst.w, dst.h, kNoTint, drawTransform);
            }
        }

        // Depth-first, a parent before its children: the traversal index is the scene order. emplace_or_replace makes a changed layer
        // or flag show up and does not allocate once the node has its item. A hidden node hides its whole subtree but keeps its rank.
        void WriteDrawItems(entt::registry& registry, SceneNode& node, uint32_t& order, bool ancestorHidden) {
            const bool hidden = ancestorHidden || node.HasEditorFlag(EditorFlags::Hidden);
            registry.emplace_or_replace<SceneDrawItem>(node.GetEntity(), node.GetLayer(), order++, &node, hidden);
            SyncRenderData(registry, node);
            for (const auto& child : node.GetChildren()) {
                WriteDrawItems(registry, *child, order, hidden);
            }
        }

    }  // namespace

    void SceneDrawSystem::Run(Scene& scene) {
        BE_PROFILE_SCOPE("SceneDrawSystem::Run");
        entt::registry& registry = scene.GetRegistry();

        {
            BE_PROFILE_SCOPE("Sync draw items");
            uint32_t order = 0;
            WriteDrawItems(registry, scene.GetRootNode(), order, false);
        }

        // The keys are unique (order is the traversal index), so the result does not depend on the sort being stable.
        // EnTT sorts so that a view iterates in the order of the comparator: the first item of the view is the lowest (layer, order).
        {
            BE_PROFILE_SCOPE("Sort draw items");
            registry.sort<SceneDrawItem>([](const SceneDrawItem& a, const SceneDrawItem& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });
        }

        // The quad and the sprite are drawn from their registry data at the place of their component in the node; any other
        // component draws itself.
        BE_PROFILE_SCOPE("Draw items");
        for (const auto [entity, item] : registry.view<SceneDrawItem>().each()) {
            if (item.hidden) {
                continue;
            }
            const TransformData* transform = registry.try_get<TransformData>(entity);
            const QuadDrawData* quad = registry.try_get<QuadDrawData>(entity);
            const SpriteDrawData* sprite = registry.try_get<SpriteDrawData>(entity);
            for (const auto& component : item.node->GetComponents()) {
                if (quad && component.Get() == quad->owner) {
                    DrawQuad(*quad, transform);
                } else if (sprite && component.Get() == sprite->owner) {
                    DrawSprite(*sprite, transform);
                } else {
                    component->OnDraw();
                }
            }
        }
    }

}  // namespace BECore
