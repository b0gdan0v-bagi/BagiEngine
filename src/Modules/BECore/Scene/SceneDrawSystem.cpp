#include "SceneDrawSystem.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Renderer/DrawTransform.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>

namespace BECore {

    namespace {

        constexpr Color kNoTint{255, 255, 255, 255};  // a sprite is drawn as it is: white leaves the texel as it is

        // The components of a node that the draw list needs. The first of each type counts, as GetComponent<T>().
        struct NodeParts {
            const TransformComponent* transform = nullptr;
            const QuadRendererComponent* quad = nullptr;
            const SpriteRendererComponent* sprite = nullptr;
            bool hasClipRect = false;

            // Whether the node clips its subtree: it has a ClipRectComponent and a TransformComponent.
            bool ClipsSubtree() const {
                return hasClipRect && transform != nullptr;
            }
        };

        // One pass over the components, by type name (a subclass is another type, as for GetComponent<T>()). Rebuild only.
        NodeParts FindParts(const SceneNode& node) {
            NodeParts parts;
            for (const auto& component : node.GetComponents()) {
                const StringView typeName = component->GetTypeMeta().typeName;
                if (!parts.transform && typeName == TransformComponent::GetStaticTypeName()) {
                    parts.transform = static_cast<const TransformComponent*>(component.Get());
                } else if (!parts.quad && typeName == QuadRendererComponent::GetStaticTypeName()) {
                    parts.quad = static_cast<const QuadRendererComponent*>(component.Get());
                } else if (!parts.sprite && typeName == SpriteRendererComponent::GetStaticTypeName()) {
                    parts.sprite = static_cast<const SpriteRendererComponent*>(component.Get());
                } else if (typeName == ClipRectComponent::GetStaticTypeName()) {
                    parts.hasClipRect = true;
                }
            }
            return parts;
        }

        // The turn of a node: about the pivot of the node's rect (not of the rect a Contain sprite is fitted into), as an absolute point
        // in scene units, which is what DrawTransform wants. flipX / flipY are the sprite's.
        DrawTransform MakeDrawTransform(const TransformComponent& transform, bool flipX, bool flipY) {
            return DrawTransform{transform._rotation, transform._x + transform._pivotX * transform._width, transform._y + transform._pivotY * transform._height, flipX, flipY};
        }

        // The blend mode is the quad's alone: the guard puts the previous one back.
        void DrawQuad(IRenderer& renderer, const QuadRendererComponent& quad, const TransformComponent& transform) {
            const ScopedBlendMode blend(renderer, quad._blend);
            const DrawTransform drawTransform = MakeDrawTransform(transform, false, false);
            if (drawTransform.IsIdentity()) {
                renderer.DrawFilledRect(transform._x, transform._y, transform._width, transform._height, quad._color);
            } else {
                renderer.DrawTransformedRect(transform._x, transform._y, transform._width, transform._height, quad._color, drawTransform);
            }
        }

        // A sprite without a texture draws nothing. An empty source rect means the whole texture.
        void DrawSprite(IRenderer& renderer, const SpriteRendererComponent& sprite, const TransformComponent& transform) {
            const ResourceHandle<ITexture>& handle = sprite.GetTexture();
            if (!handle) {
                return;
            }
            // Dereferencing the handle marks the texture as used (updates its access time)
            ITexture& texture = *handle;
            const Rect* src = sprite._srcRect.IsEmpty() ? nullptr : &sprite._srcRect;
            const float srcWidth = src ? src->w : texture.GetWidth();
            const float srcHeight = src ? src->h : texture.GetHeight();
            const Rect dst = FitSpriteRect(srcWidth, srcHeight, Rect{transform._x, transform._y, transform._width, transform._height}, sprite._fit);
            const ScopedBlendMode blend(renderer, sprite._blend);
            const DrawTransform drawTransform = MakeDrawTransform(transform, sprite._flipX, sprite._flipY);
            if (drawTransform.IsIdentity()) {
                renderer.DrawTexture(texture, src, dst.x, dst.y, dst.w, dst.h);
            } else {
                renderer.DrawTransformedTexture(texture, src, dst.x, dst.y, dst.w, dst.h, kNoTint, drawTransform);
            }
        }

        // Depth-first, a parent before its children: the traversal index is the scene order. The push_back does not allocate once the
        // vector has its capacity. A hidden node hides its whole subtree but keeps its rank. A node that clips appends its owner to @p clips.
        void WriteDrawItems(Vector<SceneDrawItem>& items, SceneNode& node, uint32_t& order, bool ancestorHidden, const SceneNode* ancestorClip, int32_t ancestorClipIndex,
                            Vector<SceneClipOwner>& clips) {
            const bool hidden = ancestorHidden || node.HasEditorFlag(EditorFlags::Hidden);
            const NodeParts parts = FindParts(node);
            const SceneNode* clip = ancestorClip;
            int32_t clipIndex = ancestorClipIndex;
            if (parts.ClipsSubtree()) {
                clip = &node;
                clipIndex = static_cast<int32_t>(clips.size());
                clips.push_back(SceneClipOwner{parts.transform, ancestorClipIndex});
            }
            items.push_back(SceneDrawItem{node.GetLayer(), order++, &node, hidden, clip, clipIndex});
            for (const auto& child : node.GetChildren()) {
                WriteDrawItems(items, *child, order, hidden, clip, clipIndex, clips);
            }
        }

        // One entry per drawing component of every visible node, in the order of the sorted items and, inside a node, of its components:
        // the order of the entries is the draw order. The first quad and the first sprite of a node draw when the node has a transform.
        void FillEntries(SceneDrawList& list) {
            for (const SceneDrawItem& item : list.items) {
                if (item.hidden) {
                    continue;
                }
                const NodeParts parts = FindParts(*item.node);
                for (const auto& component : item.node->GetComponents()) {
                    const IComponent* const current = component.Get();
                    if (current == parts.quad) {
                        if (parts.transform) {
                            list.entries.push_back(SceneDrawEntry{component.Get(), parts.transform, item.clipIndex, SceneDrawKind::Quad});
                        }
                    } else if (current == parts.sprite) {
                        if (parts.transform) {
                            list.entries.push_back(SceneDrawEntry{component.Get(), parts.transform, item.clipIndex, SceneDrawKind::Sprite});
                        }
                    } else if (component->HasOnDraw()) {
                        list.entries.push_back(SceneDrawEntry{component.Get(), nullptr, item.clipIndex, SceneDrawKind::Custom});
                    }
                }
            }
        }

        using ClipChain = FixedVector<int32_t, SceneDrawSystem::kMaxClipDepth>;

        // The clip owners of @p owner and of the ones around it, outermost first. Past the depth limit the outermost owners are left out.
        void CollectClipChain(const Vector<SceneClipOwner>& clips, int32_t owner, ClipChain& outermostFirst) {
            outermostFirst.clear();
            ClipChain nearestFirst;
            while (owner >= 0 && !nearestFirst.full()) {
                nearestFirst.push_back(owner);
                owner = clips[static_cast<size_t>(owner)].outer;
            }
            outermostFirst.assign(nearestFirst.rbegin(), nearestFirst.rend());
        }

        // The clips the draw loop has pushed on the renderer, for the owner `current` of the entry it drew last.
        struct PushedClips {
            ClipChain chain;
            ClipChain wanted;  // kept as a member so a switch builds nothing new
            int32_t current = -1;

            // Makes the renderer's clip the one of @p owner: pops down to the part the two chains share and pushes the rest.
            void Switch(IRenderer& renderer, const Vector<SceneClipOwner>& clips, const ViewTransform& view, int32_t owner) {
                if (owner == current) {
                    return;
                }
                current = owner;
                CollectClipChain(clips, owner, wanted);
                size_t shared = 0;
                while (shared < chain.size() && shared < wanted.size() && chain[shared] == wanted[shared]) {
                    ++shared;
                }
                while (chain.size() > shared) {
                    renderer.PopClipRect();
                    chain.pop_back();
                }
                for (size_t index = shared; index < wanted.size(); ++index) {
                    const TransformComponent& transform = *clips[static_cast<size_t>(wanted[index])].transform;
                    renderer.PushClipRect(view.ToOutput(transform._x, transform._y, transform._width, transform._height));
                    chain.push_back(wanted[index]);
                }
            }
        };

    }  // namespace

    void SceneDrawSystem::Run(Scene& scene) {
        BE_PROFILE_SCOPE("SceneDrawSystem::Run");
        SceneDrawList& list = scene.GetDrawList({});
        scene.SetDrawing(true, {});

        // Every edit that clears the ranks clears the list too, so the list flag says it all.
        if (!scene.IsDrawListValid()) {
            BE_PROFILE_SCOPE("Rebuild draw list");
            list.entries.clear();
            list.clips.clear();
            list.items.clear();
            {
                BE_PROFILE_SCOPE("Sync draw items");
                uint32_t order = 0;
                WriteDrawItems(list.items, scene.GetRootNode(), order, false, nullptr, -1, list.clips);
            }

            // The keys are unique (order is the traversal index), so the result does not depend on the sort being stable, and an unstable
            // sort needs no scratch buffer to allocate.
            {
                BE_PROFILE_SCOPE("Sort draw items");
                eastl::sort(list.items.begin(), list.items.end(), [](const SceneDrawItem& a, const SceneDrawItem& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });
            }

            // The rank is the index in the sorted items; written before the first OnDraw() because a component may ask for it there.
            scene.DrawRanksWritten({});

            FillEntries(list);
            scene.DrawListBuilt({});
        }

        // Without a renderer nothing is drawn by the system, but a component's OnDraw() still runs (a test, a tool).
        BE_PROFILE_SCOPE("Draw items");
        IRenderer* const renderer = CoreManager::GetRenderer().Get();
        const ViewTransform& view = scene.GetViewTransform();
        PushedClips clips;
        for (const SceneDrawEntry& entry : list.entries) {
            if (renderer != nullptr) {
                clips.Switch(*renderer, list.clips, view, entry.clip);
            }
            switch (entry.kind) {
                case SceneDrawKind::Quad:
                    if (renderer != nullptr) {
                        DrawQuad(*renderer, static_cast<const QuadRendererComponent&>(*entry.component), *entry.transform);
                    }
                    break;
                case SceneDrawKind::Sprite:
                    if (renderer != nullptr) {
                        DrawSprite(*renderer, static_cast<const SpriteRendererComponent&>(*entry.component), *entry.transform);
                    }
                    break;
                case SceneDrawKind::Custom:
                    entry.component->OnDraw();
                    break;
            }
        }
        if (renderer != nullptr) {
            clips.Switch(*renderer, list.clips, view, -1);
            renderer->Flush();  // so the zone holds the submissions and nothing stays queued past the pass
        }
        scene.SetDrawing(false, {});
    }

}  // namespace BECore
