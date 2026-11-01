#pragma once

#include <BECore/Scene/SceneNode.h>
#include <entt/entt.hpp>

namespace BECore {

    class SceneManager;

    /// One node in the draw pass of Scene::Draw(): sorted by (layer, order), both fixed for the frame.
    struct SceneDrawEntry {
        int32_t layer = 0;
        uint32_t order = 0;
        SceneNode* node = nullptr;
    };

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
         * Draw order is the order of the nodes sorted by (SceneNode::GetLayer(), scene order): a lower layer first, and
         * within a layer the depth-first order of the tree (a parent before its children, siblings as in the config), so
         * a node later in the scene is drawn on top. It does not depend on any event handler order. Nothing may add or
         * remove nodes while this runs.
         */
        void Draw();

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

    private:
        void CollectDrawEntries(SceneNode& node);

        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<SceneNode>> _nodes;
        entt::registry _registry;
        IntrusivePtr<SceneNode> _rootNode;
        eastl::vector<SceneDrawEntry> _drawEntries;  // reused every frame, so Draw() allocates only while it grows
        bool _active = false;
    };

}  // namespace BECore
