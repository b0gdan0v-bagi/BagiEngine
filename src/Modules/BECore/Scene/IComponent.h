#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/SceneUpdateList.h>
#include <Events/SubscriptionHolder.h>
#include <type_traits>

namespace BECore {

    class Scene;
    class SceneNode;

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneFixedUpdateEvent;
    }  // namespace SceneEvents

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
         * A component that updates registers in the update list of its scene from here (SubscribeInScene of SceneUpdateEvent or SceneFixedUpdateEvent); it
         * leaves the list with its node (SceneNode::RemoveChild) or by SceneNode::RemoveComponent.
         */
        virtual void OnAttached() {}

        /**
         * @brief Called after a field of this component was written by name from outside (`scene_set`); @p field is the serialized name of the top-level field.
         *
         * A component that keeps something made from a field makes it again here. A field that is read when it is used needs nothing.
         */
        virtual void OnPropertyWritten(StringView /*field*/) {}

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
         * SceneEvents::SceneUpdateEvent and SceneFixedUpdateEvent are not delivered through the global channel: the component joins the
         * update list of its scene, which Scene::Update() runs while the scene is active, the last registered first. That needs the scene,
         * so call it from OnAttached(); before that (Initialize()) the event goes through the channel with the per-call filter below, and
         * a hand SceneEvents::SceneUpdateEvent::Emit() reaches only that kind of subscriber. The component leaves the list when it leaves
         * its node (SceneNode::RemoveComponent, RemoveChild); a node that only moves stays. For every other event the connection is
         * permanent and only the call is filtered, so switching the active scene never connects or disconnects anything.
         *
         * Connecting from inside a handler of another event is undefined behavior; a component that a handler adds to a node registers in an update
         * list without harm and is first called in the next pass.
         */
        template <typename Event, auto Handler, typename T>
        void SubscribeInScene(T* instance) {
            constexpr bool isFrame = std::is_same_v<Event, SceneEvents::SceneUpdateEvent>;
            constexpr bool isFixed = std::is_same_v<Event, SceneEvents::SceneFixedUpdateEvent>;
            if constexpr (isFrame || isFixed) {
                if (_scene != nullptr) {
                    ASSERT(static_cast<IComponent*>(instance) == this);
                    RegisterSceneUpdate(isFrame ? SceneUpdateKind::Frame : SceneUpdateKind::Fixed, &InvokeSceneUpdate<Event, Handler, T>);
                    return;
                }
            }
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

        template <typename Event, auto Handler, typename T>
        static void InvokeSceneUpdate(IComponent* self, const void* event) {
            (static_cast<T*>(self)->*Handler)(*static_cast<const Event*>(event));
        }

        /// Adds this component to the update list of its scene (SubscribeInScene only); needs `_scene`
        void RegisterSceneUpdate(SceneUpdateKind kind, SceneUpdateInvoker invoke);

        Scene* _scene = nullptr;
    };

}  // namespace BECore
