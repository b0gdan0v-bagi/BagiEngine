#pragma once

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/SceneNode.h>
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

        /// Called by SceneNode::RemoveChild() only: destroys the entity of @p node and of its descendants and resets their GetEntity() to entt::null.
        void DestroyNodeEntities(SceneNode& node, PassKey<SceneNode>);

    private:
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
