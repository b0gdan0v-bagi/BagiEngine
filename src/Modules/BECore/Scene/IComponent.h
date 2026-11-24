#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Events/SubscriptionHolder.h>

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
         * @brief Called when the component is attached to a node, with `_node` and `_scene` set: by WireToScene (after deserialization) and AddComponent (runtime).
         *
         * Use Initialize() for reflection hooks (it runs even when there is no node), OnAttached() for node-dependent setup (event subscriptions, resource lookups).
         */
        virtual void OnAttached() {}

        /**
         * @brief Draw this component. Called once per frame while its scene is the active one, in the draw order of Scene::Draw().
         *
         * A component that draws itself overrides it and does not subscribe to an event for drawing: the order of event handlers is not
         * defined. QuadRendererComponent and SpriteRendererComponent have no OnDraw(): SceneDrawSystem draws them from their fields, at the
         * place of the component in the node. Draw only, never change game state here, and never add or remove nodes or components, set a
         * layer or the editor flags: the draw list holds raw pointers to the components and is rebuilt only after such a change, so it
         * would be walked while stale (checked in Debug).
         */
        virtual void OnDraw() {}

        /**
         * @brief Whether OnDraw() does anything. The draw list asks once per rebuild, never per frame, and a component that says false has no entry in it.
         *
         * True is the safe default: a component that overrides OnDraw() needs nothing here. The built-ins that never draw (and the renderers,
         * which SceneDrawSystem draws itself) say false.
         */
        virtual bool HasOnDraw() const {
            return true;
        }

        Scene& GetScene() const;
        SceneNode& GetNode() const;

        /// True while this component's scene is the active one (see Scene::IsActive()). False before the component is attached.
        bool IsSceneActive() const;

    protected:
        /**
         * @brief Subscribe to a global event, but run @p Handler only while this component's scene is active.
         *
         * Scene and input events are global: every loaded scene's components receive them, not just the active scene's. Use this instead
         * of Subscribe for any handler that reads input or changes game state; plain Subscribe is for handlers that must run whichever
         * scene is active. Drawing is not an event: override OnDraw().
         *
         * The connection itself is permanent, only the call is filtered, so switching the active scene never connects or disconnects
         * anything (doing that from inside a handler is undefined behavior).
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
