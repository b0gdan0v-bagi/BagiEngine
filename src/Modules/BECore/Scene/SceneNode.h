#pragma once

#include <BECore/Memory/Pooled.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <cstdint>

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
        BE_CLASS(SceneNode, FACTORY_BASE, ELEMENT = node, REGISTER_SELF)
    public:
        BE_POOLED_SINGLE_THREAD(SceneNode)

        /// Components a node keeps without a heap block: a transform and a renderer cover nearly every node a scene or a spawner builds
        static constexpr size_t kInlineComponents = 2;
        /// Children a node keeps without a heap block: nearly every node is a leaf and pays 8 B per slot, so one is all the inline list affords
        static constexpr size_t kInlineChildren = 1;

        using ComponentList = SmallVector<IntrusivePtr<IComponent>, kInlineComponents>;
        using ChildList = SmallVector<IntrusivePtr<SceneNode>, kInlineChildren>;

        /// Detaches the components and the children that still name this node: both may outlive it in somebody's hands
        ~SceneNode() override;

        /**
         * @brief The number of this node at run time: from 1, unique in the process and never reused
         *
         * Names a node where a pointer cannot, since the pool gives a dead node's address to a new one. No file and no copy keeps it: a duplicated or
         * a reloaded node has a new one.
         */
        [[nodiscard]] uint64_t GetRuntimeId() const {
            return _runtimeId;
        }

        SceneNode* GetParent() const;

        /// True when @p ancestor is above this node (its parent, the parent's parent, ...); a node is not its own ancestor
        bool IsDescendantOf(const SceneNode& ancestor) const;
        const ChildList& GetChildren() const;

        /**
         * @brief Makes a child node at run time, appended to the children of this node
         *
         * The child is generated (IsGenerated()): the scene editor's Save leaves it out, because the component that made it makes it again at the next
         * start. A caller that stands for the editor (the "Add node" of the scene editor) clears the flag of what it makes.
         */
        IntrusivePtr<SceneNode> AddChild(PoolString name);
        void RemoveChild(SceneNode* child);

        void AddComponent(IntrusivePtr<IComponent> component);
        /// Takes the component of this type out and detaches it (IComponent::TryGetNode() is null afterwards)
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

        /**
         * @brief True for a node made at run time (AddChild, Scene::CreateNode, Scene::InstantiateNode), false for one read from a file or made by Scene::DuplicateNode
         *
         * A generated node and its subtree are not saved (Scene::PrepareForSave) and not copied by Scene::DuplicateNode: the code that made it makes it again, so a
         * saved copy would double it. SetGenerated(false) turns a node into authored content, which is what the editor does with the nodes it adds.
         * A plain member, not a reflected field: no file, no Serialize and no copy sees it.
         */
        bool IsGenerated() const;
        void SetGenerated(bool generated);

        const ComponentList& GetComponents() const;

    private:
        friend class Scene;
        friend class IComponent;

        void WireToScene(Scene* scene, SceneNode* parent);

        /// Clears the node of @p component unless it was attached to another node since it joined this one
        void Detach(IComponent& component) const;

        /// The next id of GetRuntimeId(); any thread may make a node
        [[nodiscard]] static uint64_t NextRuntimeId();

        // The members that are not reflected sit where the alignment of their neighbours would leave padding: nodes are many
        EditorFlags _editorFlags = EditorFlags::None;
        bool _generated = false;
        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD int32_t _layer = 0;
        int32_t _drawRank = 0;  // index of the node in SceneDrawList::items, written by Scene::DrawRanksWritten(); read through Scene::GetDrawRank() only
        BE_REFLECT_FIELD PoolString _sourceRef;
        Scene* _scene = nullptr;
        SceneNode* _parent = nullptr;
        const uint64_t _runtimeId = NextRuntimeId();
        BE_REFLECT_FIELD BE_FIELD_ATTRS(no_factory) ComponentList _components;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(no_factory) ChildList _children;
    };

    /// The node with the id @p id (SceneNode::GetRuntimeId()) among @p root and its descendants, or null. A walk of the tree that allocates nothing
    [[nodiscard]] SceneNode* FindNodeByRuntimeId(SceneNode& root, uint64_t id);

}  // namespace BECore
