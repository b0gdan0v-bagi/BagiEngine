#pragma once

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Scene/SceneView.h>
#include <entt/entt.hpp>

namespace BECore {

    class SceneManager;

    class Scene : public RefCounted {
        BE_CLASS(Scene, FACTORY_BASE)
    public:
        Scene() = default;
        ~Scene() override = default;

        void Initialize();

        /**
         * @brief Emit SceneFixedUpdateEvent once per fixed step due this frame, then SceneUpdateEvent.
         *
         * Reads the frame from CoreManager::GetTime(), so call it once per frame, after Time advanced. First it sends
         * SceneEvents::SceneViewChangedEvent when the active view changed since the last send (decided in Draw(), announced here).
         */
        void Update();

        /**
         * @brief Draw every component in draw order.
         *
         * The pass is SceneDrawSystem: it writes a SceneDrawItem into the registry for every node, sorts them and walks
         * a view over them. It draws QuadRendererComponent and SpriteRendererComponent itself from their registry data
         * and calls IComponent::OnDraw() of every other component. Draw order is the order of the nodes sorted by
         * (SceneNode::GetLayer(), scene order): a lower layer first, and within a layer the depth-first order of the tree
         * (a parent before its children, siblings as in the config), so a node later in the scene is drawn on top. It does
         * not depend on any event handler order. A node has at most one quad and one sprite (the first of each counts).
         * Nothing may add or remove nodes while this runs.
         */
        void Draw();

        /**
         * @brief The place of @p node in the draw order: how many nodes Draw() draws before it. A higher rank is drawn later, so on top.
         *
         * It follows the same rule as Draw() (layer, then scene order), computed from the tree as it is now, with no draw before it needed.
         * Input handlers use it to ask the topmost first. 0 for a node that is not in this scene.
         */
        int32_t GetDrawRank(const SceneNode& node) const;

        /**
         * @brief Size of the scene's **active** view in scene units, 0 when it has none.
         *
         * The default view is the XML `viewWidth`, `viewHeight`; a scene may list `viewVariants` that replace it while the render output has
         * their shape (see GetActiveView()). With a view size the scene is drawn scaled to fit the render output and centered, so it looks
         * the same in a window of any size; without one a unit is a pixel. Everything in the scene (transforms, board origins) is in these
         * units. Before the first draw this is the default view.
         */
        float GetViewWidth() const;
        float GetViewHeight() const;

        /**
         * @brief The view the scene draws with: the default one (`index` -1, empty name) or the first variant that holds for the output.
         *
         * Decided at the start of every Draw() from the output size of that pass (SceneViewSelector::Pick), so it is right already inside
         * OnDraw(); before the first draw it is the default view. Nothing outside Draw() changes it.
         */
        ActiveSceneView GetActiveView() const;

        /// Moves up exactly when the choice (its `index`) changes, never goes back; a change of the pick is the only thing it counts
        uint32_t GetViewVersion() const;

        /// The declared view variants, read only (a tool lists them by name; a variant's index is the one `ActiveSceneView::index` carries)
        eastl::span<const SceneViewVariant> GetViewVariants() const;

        /**
         * @brief A tool's override of the pick: nullopt picks by the output as always, -1 forces the default view, `i` forces the variant `i`.
         *
         * An index out of range is the same as nullopt. It is not saved and not reflected: a tool (the editor viewport) sets it right before
         * one Draw() and clears it right after, so Game mode, other passes and Update() never see it. Setting it changes nothing by itself;
         * the next Draw() picks, and bumps the version and announces the change like any other pick.
         */
        void SetViewOverride(std::optional<int32_t> index);

        /**
         * @brief Map a mouse position in render output pixels (window or viewport-local) to scene units.
         *
         * Uses the transform of the last Draw(); the identity before the first draw and for a scene without a view
         * size. Mouse handlers compare their event position with scene geometry through these.
         */
        float ScreenToSceneX(float outputX) const;
        float ScreenToSceneY(float outputY) const;

        /**
         * @brief True while this is the scene that SceneManager updates and draws.
         *
         * Scene events and input events are global, so every loaded scene's components receive them;
         * IComponent::SubscribeInScene uses this flag to let only the active scene's components react.
         * A scene that no SceneManager has activated is never active.
         */
        bool IsActive() const;

        /// Called by SceneManager only, when the active scene changes.
        void SetActive(bool active, PassKey<SceneManager>);

        /**
         * @brief Sync the runtime hierarchy back into the serializable _nodes field.
         *        Call before saving so that nodes added at runtime are included.
         */
        void PrepareForSave();

        entt::registry& GetRegistry();
        SceneNode& GetRootNode();
        PoolString GetName() const;

        IntrusivePtr<SceneNode> CreateNode(PoolString name, SceneNode* parent = nullptr);

        /// The @p index of InstantiateNode() that means "after the last child"
        static constexpr size_t kAppend = std::numeric_limits<size_t>::max();

        /**
         * @brief Instantiates a node file at run time, with the overrides a `<node ref>` of a scene file would have
         *
         * @p reference is a `<node ref=".." name=".." layer="..">` element with the same overrides as in a scene file
         * (ApplyNodeOverrides), so a game writes the overrides as XML text and loads it with XmlDocument: there is no second
         * override language. The expansion is ExpandNodeReference, the same function that expands the scene file, with the
         * source mark: the new node answers SceneNode::GetSourceRef(). The node is wired as a loaded one is (entity, then
         * IComponent::OnAttached of every component) and inserted into the children of @p parent at @p index, so its draw
         * and input order follow from the tree. It has no children of its own.
         *
         * A reference that cannot be used (invalid, a file that is missing or of another content version, a bad override), a
         * scene that is not initialized and a @p parent that is not a live node of this scene are one LOG_ERROR that names the
         * scene and the reference; an empty pointer is returned and the scene is not changed.
         *
         * @param reference The `<node ref="..">` element
         * @param loader Reads the node file of the reference
         * @param parent The parent node, the scene's root when null
         * @param index Place among the children of the parent, clamped to their number; kAppend is the last
         * @return The new node (also held by its parent), empty on failure
         */
        IntrusivePtr<SceneNode> InstantiateNode(pugi::xml_node reference, const NodeFileLoader& loader, SceneNode* parent = nullptr, size_t index = kAppend);

        /// InstantiateNode() of a reference without overrides (`nodes/MahjongHud`), the node file found through the FileSystem of CoreManager (a game's `config/nodes/X.xml` wins over the engine's)
        IntrusivePtr<SceneNode> InstantiateNode(eastl::string_view ref, SceneNode* parent = nullptr, size_t index = kAppend);

        /// InstantiateNode() of a reference element with overrides, the node file found through the FileSystem of CoreManager
        IntrusivePtr<SceneNode> InstantiateNode(const XmlNode& reference, SceneNode* parent = nullptr, size_t index = kAppend);

        /**
         * @brief Moves @p node under @p newParent (the root when null) at @p index among its children, in this scene
         *
         * @p index counts the children of the new parent **after @p node has left its old place**, so moving a node down inside one parent to
         * index `n` puts it behind `n` of the others; it is clamped, kAppend is the last. The node keeps its entity, its components and its
         * subtree (no second `OnAttached`), and the draw order follows the tree, so nothing else is updated.
         *
         * Refused, with nothing changed, and false returned: the root, a node of another scene, a parent of another scene, and a parent that
         * is the node itself or below it.
         */
        bool MoveNode(SceneNode& node, SceneNode* newParent, size_t index = kAppend);

        /**
         * @brief Copies @p node and everything below it, placed right after it among the children of its parent
         *
         * The copy is read from the node's inline XML (AppendNodeElement, DeserializeNode) and wired as InstantiateNode wires a node (entity,
         * `OnAttached` of every component), so it shares nothing with the original. Its name is `Name (copy)`, or `Name (copy 2)` and up while
         * a sibling has it. The top node keeps SceneNode::GetSourceRef(), so a copied instance is an instance.
         *
         * @return The copy, empty for the root and for a node that is not in this scene (nothing changed)
         */
        IntrusivePtr<SceneNode> DuplicateNode(const SceneNode& node);

        /// @p base when no child of @p parent is called so, else `base 2`, `base 3`, ... the first one that is free
        PoolString MakeUniqueSiblingName(const SceneNode& parent, eastl::string_view base) const;

        /// Called by SceneNode::RemoveChild() only: destroys the entity of @p node and of its descendants and resets their GetEntity() to entt::null.
        void DestroyNodeEntities(SceneNode& node, PassKey<SceneNode>);

    private:
        bool IsLiveNode(const SceneNode& node) const;
        void DestroyEntities(SceneNode& node);
        void PickView(float outputWidth, float outputHeight);

        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD float _viewWidth = 0.0f;
        BE_REFLECT_FIELD float _viewHeight = 0.0f;
        BE_REFLECT_FIELD eastl::vector<SceneViewVariant> _viewVariants;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<SceneNode>> _nodes;
        entt::registry _registry;
        IntrusivePtr<SceneNode> _rootNode;
        ViewTransform _viewTransform;   // fit of the view to the output, set by Draw(), read by ScreenToScene*
        int32_t _activeViewIndex = -1;  // index in _viewVariants, -1 the default view; set by Draw()
        uint32_t _viewVersion = 0;
        uint32_t _announcedViewVersion = 0;    // the version Update() last sent SceneViewChangedEvent for
        std::optional<int32_t> _viewOverride;  // a tool's forced view, see SetViewOverride(); never saved
        bool _active = false;
    };

}  // namespace BECore
