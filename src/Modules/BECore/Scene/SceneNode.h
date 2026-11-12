#pragma once

#include <BECore/Scene/IComponent.h>

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
        SceneNode() = default;
        ~SceneNode() override = default;

        SceneNode* GetParent() const;

        /// True when @p ancestor is above this node (its parent, the parent's parent, ...); a node is not its own ancestor
        bool IsDescendantOf(const SceneNode& ancestor) const;
        const eastl::vector<IntrusivePtr<SceneNode>>& GetChildren() const;
        IntrusivePtr<SceneNode> AddChild(PoolString name);
        void RemoveChild(SceneNode* child);

        void AddComponent(IntrusivePtr<IComponent> component);
        void RemoveComponent(PoolString typeName);
        IntrusivePtr<IComponent> GetComponent(eastl::string_view typeName) const;

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
         * Nodes with the same layer are drawn in scene order. The layer belongs to the node alone, its children do not
         * inherit it. See Scene::Draw().
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
        const eastl::vector<IntrusivePtr<IComponent>>& GetComponents() const;

    private:
        friend class Scene;

        void WireToScene(Scene* scene, SceneNode* parent);

        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD int32_t _layer = 0;
        BE_REFLECT_FIELD PoolString _sourceRef;
        entt::entity _entity = entt::null;
        Scene* _scene = nullptr;
        SceneNode* _parent = nullptr;
        EditorFlags _editorFlags = EditorFlags::None;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<IComponent>> _components;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<SceneNode>> _children;
    };

}  // namespace BECore
