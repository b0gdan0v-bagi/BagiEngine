#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    class Scene;

    class SceneNode : public RefCounted {
        BE_CLASS(SceneNode, FACTORY_BASE, ELEMENT = node)
    public:
        SceneNode() = default;
        ~SceneNode() override = default;

        SceneNode* GetParent() const;
        const eastl::vector<IntrusivePtr<SceneNode>>& GetChildren() const;
        IntrusivePtr<SceneNode> AddChild(PoolString name);
        void RemoveChild(SceneNode* child);

        void AddComponent(IntrusivePtr<IComponent> component);
        void RemoveComponent(PoolString typeName);
        IntrusivePtr<IComponent> GetComponent(eastl::string_view typeName) const;

        template <typename T>
        IntrusivePtr<T> GetComponent() const {
            auto base = GetComponent(T::GetStaticTypeName());
            if (!base)
                return {};
            return IntrusivePtr<T>(static_cast<T*>(base.Get()));
        }

        PoolString GetName() const;
        entt::entity GetEntity() const;

    private:
        friend class Scene;

        void WireToScene(Scene* scene, SceneNode* parent);

        BE_REFLECT_FIELD PoolString _name;
        entt::entity _entity = entt::null;
        Scene* _scene = nullptr;
        SceneNode* _parent = nullptr;
        eastl::vector<IntrusivePtr<SceneNode>> _children;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<IComponent>> _components;
    };

}  // namespace BECore
