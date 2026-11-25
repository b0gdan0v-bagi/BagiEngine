#pragma once

#include <BECore/Config/XmlNode.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/SceneDrawList.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Scene/SceneUpdateList.h>
#include <BECore/Scene/SceneView.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/PassKey.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace BECore {

    class SceneManager;
    class SceneDrawSystem;
    class IComponent;

    class Scene : public RefCounted {
        BE_CLASS(Scene, FACTORY_BASE)
    public:
        void Initialize();

        /**
         * @brief Update the scene: first the fixed update list once per fixed step due this frame, then the frame list.
         *
         * Each pass runs the update list of the scene (components that registered with SubscribeInScene of SceneFixedUpdateEvent or
         * SceneUpdateEvent; see IComponent::OnAttached()) and then emits the event for the plain subscribers, which are not scene bound. The
         * lists run only while the scene is active, read once at the start: an inactive scene updates no component, and a handler that
         * switches the active scene does not cut the pass. Last registered runs first; a component a handler adds first runs in the next pass,
         * one a handler removes (node or component) and that has not run yet does not run.
         *
         * Reads the frame from CoreManager::GetTime(), so call it once per frame, after Time advanced. First it sends
         * SceneEvents::SceneViewChangedEvent when the active view changed since the last send (decided in Draw(), announced here).
         */
        void Update();

        /**
         * @brief Draw every component in draw order: by SceneNode::GetLayer(), then scene order (depth-first, a parent before its children).
         *
         * SceneDrawSystem walks the draw list (SceneDrawList): it draws QuadRendererComponent and SpriteRendererComponent itself (the first of
         * each per node) and calls IComponent::OnDraw() of every other component that has one; no event handler order is involved. The list is
         * rebuilt only when a node, a component, a layer or the editor flags changed since the last build; a field of a component is read at draw
         * time, so a change of it shows in the next draw.
         * Nothing may add or remove nodes or components, or change a layer or the editor flags, while this runs (checked in Debug).
         */
        void Draw();

        /**
         * @brief The place of @p node in the draw order: how many nodes Draw() draws before it. A higher rank is drawn later, so on top.
         *
         * No draw needed. After a Draw() it is read from the pass (O(1)) until the tree changes (a node added, removed or moved, a layer set
         * with SceneNode::SetLayer()); then, and before the first draw, it is a walk of the tree. A layer field written without SetLayer()
         * is seen for the asked node at once, for the ranks of the others only after the next rebuild of the draw list (the draw order itself
         * follows SetLayer()). 0 for a node that is not in this scene.
         */
        int32_t GetDrawRank(const SceneNode& node) const;

        /// Components of one node that GetInputOrder() tells apart: a later one is drawn on top. The factor of a node's rank in the key
        static constexpr int32_t kMaxComponentsPerNode = 256;

        /**
         * @brief The key of @p component in the input order of the scene: `GetDrawRank(node) * kMaxComponentsPerNode + index`, the index being its place in the node's components.
         *
         * A higher key is drawn later, so on top. Every handler of UIInput that has a place in this scene registers with it, so a canvas and
         * a scene target are asked in the order they cover each other. A component past the last place shares the key of the last one.
         */
        int32_t GetInputOrder(const IComponent& component) const;

        /**
         * @brief How many times Draw() started a pass; 0 while the scene never drew.
         *
         * A component that is a target of the pointer stamps it in OnDraw() and counts as drawn only while its stamp is the current pass: a node that
         * did not draw (hidden, a scene that is not active) takes no input. While it is 0 nothing was drawn at all (a test) and everything counts as drawn.
         */
        uint32_t GetDrawPass() const;

        /**
         * @brief Size of the scene's **active** view in scene units, 0 when it has none (then a unit is an output pixel).
         *
         * The default view is the XML `viewWidth`, `viewHeight`; a scene may list `viewVariants` that replace it while the render output has
         * their shape (see GetActiveView()). Everything in the scene (transforms, board origins) is in these units.
         * Before the first draw this is the default view.
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

        /// Moves up exactly when the choice (its `index`) changes, never goes back
        uint32_t GetViewVersion() const;

        /// The declared view variants, read only (a tool lists them by name; a variant's index is the one `ActiveSceneView::index` carries)
        Span<const SceneViewVariant> GetViewVariants() const;

        /**
         * @brief A tool's override of the pick: nullopt picks by the output as always, -1 forces the default view, `i` forces the variant `i`.
         *
         * An index out of range is the same as nullopt. It is not saved and not reflected: a tool (the editor viewport) sets it right before
         * one Draw() and clears it right after, so Game mode, other passes and Update() never see it. Setting it changes nothing by itself;
         * the next Draw() picks, and bumps the version and announces the change like any other pick.
         */
        void SetViewOverride(Optional<int32_t> index);

        /**
         * @brief Map a mouse position in render output pixels (window or viewport-local) to scene units.
         *
         * Uses the transform of the last Draw(); the identity before the first draw and for a scene without a view
         * size. Mouse handlers compare their event position with scene geometry through these.
         */
        float ScreenToSceneX(float outputX) const;
        float ScreenToSceneY(float outputY) const;

        /**
         * @brief The fit of the last Draw(): scene units to render output pixels (`ViewTransform::ToOutput`); the identity before the first draw and for a scene without a view size.
         *
         * The inverse of ScreenToSceneX/Y. A tool takes a scene rectangle to the pixels of the pass that drew it.
         */
        [[nodiscard]] const ViewTransform& GetViewTransform() const;

        /**
         * @brief True while this is the scene that SceneManager updates and draws; never for a scene no SceneManager has activated.
         *
         * Scene and input events are global, so IComponent::SubscribeInScene uses this flag to let only the active scene's components react.
         */
        bool IsActive() const;

        /// Called by SceneManager only, when the active scene changes.
        void SetActive(bool active, PassKey<SceneManager>);

        /**
         * @brief Sync the runtime hierarchy back into the serializable _nodes field.
         *        Call before saving so that nodes added at runtime are included.
         */
        void PrepareForSave();

        SceneNode& GetRootNode();
        PoolString GetName() const;

        IntrusivePtr<SceneNode> CreateNode(PoolString name, SceneNode* parent = nullptr);

        /// The @p index of InstantiateNode() that means "after the last child"
        static constexpr size_t kAppend = (std::numeric_limits<size_t>::max)();

        /**
         * @brief Instantiates a node file at run time, with the overrides a `<node ref>` of a scene file would have
         *
         * @p reference is a `<node ref=".." name=".." layer="..">` element with the overrides of ApplyNodeOverrides, which a game writes as
         * XML text and loads with XmlDocument. It is expanded by ExpandNodeReference with the source mark, so the new node answers
         * SceneNode::GetSourceRef(), wired as a loaded node is (IComponent::OnAttached of every component) and inserted into
         * the children of @p parent at @p index; its draw and input order follow from the tree. It has no children of its own.
         *
         * A reference that cannot be used, a scene that is not initialized and a @p parent that is not a live node of this scene are one
         * LOG_ERROR that names the scene and the reference; an empty pointer is returned and the scene is not changed.
         *
         * @param loader Reads the node file of the reference
         * @param parent The parent node, the scene's root when null
         * @param index Place among the children of the parent, clamped to their number; kAppend is the last
         * @return The new node (also held by its parent), empty on failure
         */
        IntrusivePtr<SceneNode> InstantiateNode(pugi::xml_node reference, NodeFileLoaderRef loader, SceneNode* parent = nullptr, size_t index = kAppend);

        /// InstantiateNode() of a reference without overrides (`nodes/MahjongHud`), the node file found through the FileSystem of CoreManager (a game's `config/nodes/X.xml` wins over the engine's)
        IntrusivePtr<SceneNode> InstantiateNode(StringView ref, SceneNode* parent = nullptr, size_t index = kAppend);

        /// InstantiateNode() of a reference element with overrides, the node file found through the FileSystem of CoreManager
        IntrusivePtr<SceneNode> InstantiateNode(const XmlNode& reference, SceneNode* parent = nullptr, size_t index = kAppend);

        /**
         * @brief Moves @p node under @p newParent (the root when null) at @p index among its children, in this scene
         *
         * @p index counts the children of the new parent **after @p node has left its old place**, so moving a node down inside one parent to
         * index `n` puts it behind `n` of the others; it is clamped, kAppend is the last. The node keeps its components and its
         * subtree (no second `OnAttached`).
         *
         * Refused, with nothing changed, and false returned: the root, a node or a parent that is not in the tree of this scene
         * (another scene, or taken out by RemoveChild()), and a parent that is the node itself or below it.
         */
        bool MoveNode(SceneNode& node, SceneNode* newParent, size_t index = kAppend);

        /**
         * @brief Copies @p node and everything below it, placed right after it among the children of its parent
         *
         * The copy is read from the node's inline XML (AppendNodeElement, DeserializeNode) and wired as InstantiateNode wires a node (`OnAttached` of every
         * component), so it shares nothing with the original. Its name is `Name (copy)`, or `Name (copy 2)` and up while
         * a sibling has it. The top node keeps SceneNode::GetSourceRef(), so a copied instance is an instance.
         *
         * @return The copy, empty for the root and for a node that is not in this scene (nothing changed)
         */
        IntrusivePtr<SceneNode> DuplicateNode(const SceneNode& node);

        /// @p base when no child of @p parent is called so, else `base 2`, `base 3`, ... the first one that is free
        PoolString MakeUniqueSiblingName(const SceneNode& parent, StringView base) const;

        /// Clears the editor flags (SceneNode::GetEditorFlags()) of every node of the tree, the root included. The scene editor calls it when it goes away
        void ClearEditorFlags();

        /// Called by SceneNode::RemoveChild() only, before the node leaves its parent: the draw order is stale, and the components of @p node and of its descendants leave the update lists
        void NodeRemoved(SceneNode& node, PassKey<SceneNode>);

        /// Called by IComponent::SubscribeInScene() only, through RegisterSceneUpdate(): @p component joins the update list of @p kind, unless its node is not in the tree under the root (removed)
        void AddUpdater(SceneUpdateKind kind, IComponent& component, SceneUpdateInvoker invoke, PassKey<IComponent>);

        /// Called by SceneNode::RemoveComponent() only, before the component can die: @p component leaves both update lists
        void RemoveUpdaters(IComponent& component, PassKey<SceneNode>);

        /// How many components will be called by the list of @p kind in its next run
        size_t GetUpdaterCount(SceneUpdateKind kind) const;

        /// Called by SceneNode::SetLayer() only: the ranks of the last pass no longer hold
        void NodeLayerChanged(PassKey<SceneNode>);

        /// Called by SceneNode::AddComponent() and RemoveComponent() only: the draw list no longer holds, the ranks do
        void ComponentsChanged(PassKey<SceneNode>);

        /// Called by SceneNode::SetEditorFlags() only, when the flags changed (Hidden hides a subtree from the draw list): the draw list no longer holds, the ranks do
        void EditorFlagsChanged(PassKey<SceneNode>);

        /// False when the next draw pass has to rebuild the draw list: a node, a component, a layer or the editor flags changed since the last build
        bool IsDrawListValid() const;

        /// Called by SceneDrawSystem::Run() only, once the draw items are sorted: writes the rank of every node (its index in SceneDrawList::items)
        void DrawRanksWritten(PassKey<SceneDrawSystem>);

        /// How many times SceneDrawSystem rebuilt the draw list; a steady frame leaves it as it is
        uint32_t GetDrawListBuilds() const;

        /// SceneDrawSystem::Run() only: the list it fills and walks
        SceneDrawList& GetDrawList(PassKey<SceneDrawSystem>);

        /// The list of the last draw pass; its `items` are the draw order, one per node
        const SceneDrawList& GetDrawList() const;

        /// Called by SceneDrawSystem::Run() only, once it has filled the draw list: the list holds until the next change
        void DrawListBuilt(PassKey<SceneDrawSystem>);

        /// Called by SceneDrawSystem::Run() only: true while the draw pass runs, when no node or component may change
        void SetDrawing(bool drawing, PassKey<SceneDrawSystem>);

    private:
        /// @p node belongs to this scene and its parents lead up to the root: false for a node RemoveChild() took out, and for one made under it
        bool IsLiveNode(const SceneNode& node) const;
        /// True when following the parents from @p node ends at the root of this scene
        bool IsInTree(const SceneNode& node) const;
        void ReleaseSubtree(SceneNode& node);
        SceneUpdateList& GetUpdateList(SceneUpdateKind kind);
        void DropUpdaters(IComponent& component);
        void PickView(float outputWidth, float outputHeight);

        /// A node was added, removed, moved or changed its layer: both the ranks and the draw list are stale
        void InvalidateDrawOrder();

        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD float _viewWidth = 0.0f;
        BE_REFLECT_FIELD float _viewHeight = 0.0f;
        BE_REFLECT_FIELD Vector<SceneViewVariant> _viewVariants;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(no_factory) Vector<IntrusivePtr<SceneNode>> _nodes;
        IntrusivePtr<SceneNode> _rootNode;
        ViewTransform _viewTransform;   // fit of the view to the output, set by Draw(), read by ScreenToScene*
        int32_t _activeViewIndex = -1;  // index in _viewVariants, -1 the default view; set by Draw()
        uint32_t _viewVersion = 0;
        uint32_t _announcedViewVersion = 0;  // the version Update() last sent SceneViewChangedEvent for
        Optional<int32_t> _viewOverride;     // a tool's forced view, see SetViewOverride(); never saved
        bool _active = false;
        uint32_t _drawPass = 0;
        bool _drawRanksValid = false;  // SceneNode::_drawRank is what the tree says: set by the draw system, cleared by every edit of the tree
        bool _drawing = false;         // a draw pass is running: the hooks that make the list stale must not be called
        uint32_t _drawListBuilds = 0;
        SceneDrawList _drawList;
        SceneUpdateList _frameUpdaters;
        SceneUpdateList _fixedUpdaters;
        bool _drawListValid = false;  // what the draw list was built from still holds: cleared by every edit of the tree, of the components of a node and of the editor flags
    };

}  // namespace BECore
