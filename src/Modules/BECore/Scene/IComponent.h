#pragma once

namespace BECore {

    class Scene;
    class SceneNode;

    class IComponent : public RefCounted, public SubscriptionHolder {
        BE_CLASS(IComponent, FACTORY_BASE)
    public:
        IComponent() = default;
        ~IComponent() override = default;

        virtual void Initialize() {}

        /**
         * @brief Called when component is attached to a node with _node and _scene guaranteed to be set.
         *
         * This is called after both `_node` and `_scene` are initialized, so it's safe to access the node.
         * Called by WireToScene (after deserialization) and AddComponent (runtime).
         *
         * Use Initialize() for reflection system hooks (called even when _node is null).
         * Use OnAttached() for node-dependent setup (event subscriptions, resource loading, etc).
         */
        virtual void OnAttached() {}

        /**
         * @brief Draw this component. Called once per frame while its scene is the active one.
         *
         * Scene::Draw() calls it in draw order: nodes sorted by SceneNode::GetLayer(), equal layers in scene order
         * (depth-first, a parent before its children, siblings as in the config), the components of a node in their order.
         * A component that draws itself overrides it and does not subscribe to an event for drawing: the order of event
         * handlers is not defined. QuadRendererComponent and SpriteRendererComponent have no OnDraw(): SceneDrawSystem draws
         * them from the registry, at the place of the component in the node. Draw only, never change game state or add and
         * remove nodes here.
         */
        virtual void OnDraw() {}

        Scene& GetScene() const;
        SceneNode& GetNode() const;

        /// True while this component's scene is the active one (see Scene::IsActive()). False before the component is attached.
        bool IsSceneActive() const;

    protected:
        /**
         * @brief Subscribe to a global event, but run @p Handler only while this component's scene is active.
         *
         * Scene events (SceneUpdateEvent) and input events (MouseButtonDownEvent, ActionEvent) are global:
         * every loaded scene's components receive them, not just the active scene's. Use this instead of Subscribe for
         * any handler that reads input or changes game state. Plain Subscribe is for handlers that must run
         * whichever scene is active. Drawing is not an event: override OnDraw().
         *
         * The connection itself is permanent, only the call is filtered, so switching the active scene never connects
         * or disconnects anything (doing that from inside a handler is undefined behavior).
         *
         * @example
         * SubscribeInScene<SceneEvents::SceneUpdateEvent, &MyComponent::OnUpdate>(this);
         */
        template <typename Event, auto Handler, typename T>
        void SubscribeInScene(T* instance) {
            Subscribe<Event, &InvokeInActiveScene<Event, Handler, T>>(instance);
        }

        SceneNode* _node = nullptr;

    private:
        friend class SceneNode;

        template <typename Event, auto Handler, typename T>
        static void InvokeInActiveScene(T* self, const Event& event) {
            if (self->IsSceneActive()) {
                (self->*Handler)(event);
            }
        }

        Scene* _scene = nullptr;
    };

}  // namespace BECore
