#pragma once

#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/SceneNode.h>
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
         * Reads the frame from CoreManager::GetTime(), so call it once per frame, after Time advanced.
         */
        void Update();

        /**
         * @brief Call IComponent::OnDraw() of every component in draw order.
         *
         * The pass is SceneDrawSystem: it writes a SceneDrawItem into the registry for every node, sorts them and walks
         * a view over them. Draw order is the order of the nodes sorted by (SceneNode::GetLayer(), scene order): a lower
         * layer first, and within a layer the depth-first order of the tree (a parent before its children, siblings as in
         * the config), so a node later in the scene is drawn on top. It does not depend on any event handler order.
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
         * @brief Size of the scene's view in scene units (XML `viewWidth`, `viewHeight`), 0 when it has none.
         *
         * With a view size the scene is drawn scaled to fit the render output and centered, so it looks the same in a
         * window of any size; without one a unit is a pixel. Everything in the scene (transforms, board origins) is
         * in these units.
         */
        float GetViewWidth() const;
        float GetViewHeight() const;

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

        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD float _viewWidth = 0.0f;
        BE_REFLECT_FIELD float _viewHeight = 0.0f;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<SceneNode>> _nodes;
        entt::registry _registry;
        IntrusivePtr<SceneNode> _rootNode;
        ViewTransform _viewTransform;  // fit of the view to the output, set by Draw(), read by ScreenToScene*
        bool _active = false;
    };

}  // namespace BECore
