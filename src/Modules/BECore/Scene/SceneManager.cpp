#include "SceneManager.h"

#include <Generated/SceneManager.gen.hpp>

namespace BECore {

    void SceneManager::Initialize() {
        _activeScene = _scenes.empty() ? nullptr : _scenes[0].Get();
    }

    void SceneManager::UpdateAll() const {
        if (_activeScene)
            _activeScene->Update();
    }

    void SceneManager::DrawAll() const {
        if (_activeScene)
            _activeScene->Draw();
    }

    Scene* SceneManager::GetActiveScene() const {
        return _activeScene;
    }

}  // namespace BECore
