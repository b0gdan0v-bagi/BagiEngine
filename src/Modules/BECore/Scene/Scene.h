#pragma once

#include <BECore/Scene/SceneNode.h>
#include <entt/entt.hpp>

namespace BECore {

    class Scene : public RefCounted {
        BE_CLASS(Scene, FACTORY_BASE)
    public:
        Scene() = default;
        ~Scene() override = default;

        void Initialize();

        void Update();
        void Draw();

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
        BE_REFLECT_FIELD PoolString _name;
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<SceneNode>> _nodes;
        entt::registry _registry;
        IntrusivePtr<SceneNode> _rootNode;
    };

}  // namespace BECore
