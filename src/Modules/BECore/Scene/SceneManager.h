#pragma once

#include <BECore/Scene/Scene.h>

namespace BECore {

    class SceneManager : public RefCountedAtomic {
        BE_CLASS(SceneManager)
    public:
        void Initialize();
        void UpdateAll() const;
        void DrawAll() const;

        Scene* GetActiveScene() const;

    private:
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<Scene>> _scenes;
        Scene* _activeScene = nullptr;
    };

}  // namespace BECore
