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

        Scene& GetScene() const;
        SceneNode& GetNode() const;

    protected:
        SceneNode* _node = nullptr;

    private:
        friend class SceneNode;
        Scene* _scene = nullptr;
    };

}  // namespace BECore
