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

        /**
         * @brief Activate a scene by its name (Scene::GetName()).
         *
         * Looks up the scene in _scenes by interned name. No-op if name is empty,
         * already active, or unknown (logged as error). Used by GameManager to switch
         * between game-specific scenes.
         */
        void SetActiveScene(PoolString name);

        /// Find a loaded scene by name, or nullptr if missing.
        Scene* GetSceneByName(PoolString name) const;

    private:
        BE_REFLECT_FIELD eastl::vector<IntrusivePtr<Scene>> _scenes;
        Scene* _activeScene = nullptr;
    };

}  // namespace BECore
