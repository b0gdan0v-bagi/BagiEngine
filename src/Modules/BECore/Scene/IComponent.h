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

        Scene& GetScene() const;
        SceneNode& GetNode() const;

    protected:
        SceneNode* _node = nullptr;

    private:
        friend class SceneNode;
        Scene* _scene = nullptr;
    };

}  // namespace BECore
