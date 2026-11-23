#pragma once

#include <BECore/Memory/Pooled.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <entt/entity/fwd.hpp>

namespace BECore {

    class Scene;

    /// Editor-only state of a node (a bit set, so a plain enum with operators, not CORE_ENUM): never serialized, never saved, never copied by Scene::DuplicateNode
    enum class EditorFlags : uint8_t {
        None = 0,
        Hidden = 1,  ///< the node and its descendants draw nothing; they keep their draw rank and still update
        Locked = 2,  ///< the editor refuses to rename, delete, move or edit the node
    };

    constexpr EditorFlags operator|(EditorFlags a, EditorFlags b) {
        return static_cast<EditorFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    constexpr EditorFlags operator&(EditorFlags a, EditorFlags b) {
        return static_cast<EditorFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    constexpr EditorFlags operator~(EditorFlags a) {
        return static_cast<EditorFlags>(~static_cast<uint8_t>(a));
    }

    class SceneNode : public RefCounted {
        BE_CLASS(SceneNode, FACTORY_BASE, ELEMENT = node)
    public:
        BE_POOLED_SINGLE_THREAD(SceneNode)

        /// Components a node keeps without a heap block: a transform and a renderer cover nearly every node a scene or a spawner builds
        static constexpr size_t kInlineComponents = 2;
        /// Children a node keeps without a heap block: nearly every node is a leaf and pays 8 B per slot, so one is all the inline list affords
        static constexpr size_t kInlineChildren = 1;

        using ComponentList = SmallVector<IntrusivePtr<IComponent>, kInlineComponents>;
        using ChildList = SmallVector<IntrusivePtr<SceneNode>, kInlineChildren>;

        SceneNode();
        ~SceneNode() override = default;

        SceneNode* GetParent() const;

        /// True when @p ancestor is above this node (its parent, the parent's parent, ...); a node is not its own ancestor
        bool IsDescendantOf(const SceneNode& ancestor) const;
        const ChildList& GetChildren() const;
        IntrusivePtr<SceneNode> AddChild(PoolString name);
        void RemoveChild(SceneNode* child);

        void AddComponent(IntrusivePtr<IComponent> component);
        void RemoveComponent(PoolString typeName);
        IntrusivePtr<IComponent> GetComponent(StringView typeName) const;

        template <typename T>
        IntrusivePtr<T> GetComponent() const {
            auto base = GetComponent(T::GetStaticTypeName());
            if (!base)
                return {};
            return IntrusivePtr<T>(static_cast<T*>(base.Get()));
        }

        PoolString GetName() const;
        void SetName(PoolString name);

        /**
         * @brief Draw layer of this node: a lower layer is drawn first (further back).
         *
         * The layer belongs to the node alone, its children do not inherit it. See Scene::Draw().
         */
        int32_t GetLayer() const;
        void SetLayer(int32_t layer);

        /**
         * @brief The node file this node was expanded from (`nodes/MahjongHud`), empty for a node written inline
         *
         * Set by the scene loader for a `<node ref>` and by Scene::InstantiateNode; read by the editor save, which writes
         * the node back as a reference with its overrides. Nothing else writes it, and a hand-written `sourceRef` in a
         * scene file is not supported.
         */
        PoolString GetSourceRef() const;
        void SetSourceRef(PoolString sourceRef);

        /**
         * @brief Editor-only flags of this node (hidden, locked)
         *
         * A plain member, not a reflected field: no file, no Serialize and no copy sees it, and the scene editor resets it when it
         * goes away (Scene::ClearEditorFlags()). SceneDrawSystem skips drawing a node that is hidden or has a hidden ancestor.
         */
        EditorFlags GetEditorFlags() const;
        void SetEditorFlags(EditorFlags flags);
        bool HasEditorFlag(EditorFlags flag) const;

        entt::entity GetEntity() const;
        const ComponentList& GetComponents() const;

    private:
        friend class Scene;

        void WireToScene(Scene* scene, SceneNode* parent);

        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD int32_t _layer = 0;
        BE_REFLECT_FIELD PoolString _sourceRef;
        entt::entity _entity;  // entt::null until Scene makes the entity
        Scene* _scene = nullptr;
        SceneNode* _parent = nullptr;
        EditorFlags _editorFlags = EditorFlags::None;
        BE_REFLECT_FIELD ComponentList _components;
        BE_REFLECT_FIELD ChildList _children;
    };

}  // namespace BECore
