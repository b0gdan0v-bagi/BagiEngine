#include "SceneManager.h"

#include <Generated/SceneManager.gen.hpp>

namespace BECore {

    void SceneManager::Initialize() {
        Activate(_scenes.empty() ? nullptr : _scenes[0].Get());
    }

    void SceneManager::UpdateAll() const {
        if (_activeScene) {
            _activeScene->Update();
        }
    }

    void SceneManager::DrawAll() const {
        if (_activeScene) {
            _activeScene->Draw();
        }
    }

    Scene* SceneManager::GetActiveScene() const {
        return _activeScene;
    }

    void SceneManager::SetActiveScene(PoolString name) {
        if (name.Empty()) {
            return;
        }
        Scene* scene = GetSceneByName(name);
        if (!scene) {
            LOG_ERROR(Format("SceneManager: scene '{}' not found", name.ToStringView()).c_str());
            return;
        }
        if (_activeScene == scene) {
            return;
        }
        Activate(scene);
    }

    void SceneManager::Activate(Scene* scene) {
        if (_activeScene) {
            _activeScene->SetActive(false, {});
        }
        _activeScene = scene;
        if (_activeScene) {
            _activeScene->SetActive(true, {});
        }
    }

    Scene* SceneManager::GetSceneByName(PoolString name) const {
        for (const auto& scene : _scenes) {
            if (scene && scene->GetName() == name) {
                return scene.Get();
            }
        }
        return nullptr;
    }

}  // namespace BECore
