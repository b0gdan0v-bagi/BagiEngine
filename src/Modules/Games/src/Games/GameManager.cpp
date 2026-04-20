#include "GameManager.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Scene/SceneManager.h>

namespace BECore {

    void GameManager::Initialize() {
        const auto& metas = AbstractFactory<IGame>::GetInstance().GetRegisteredTypes();
        _games.reserve(metas.size());
        for (const auto& meta : metas) {
            auto game = AbstractFactory<IGame>::GetInstance().Create(meta);
            if (!game) {
                LOG_ERROR(Format("GameManager: failed to create IGame of type '{}'", meta.typeName).c_str());
                continue;
            }
            _games.push_back(std::move(game));
        }

        LOG_INFO(Format("GameManager: registered {} game(s)", _games.size()).c_str());

        if (_games.empty()) {
            LOG_INFO("GameManager: no games registered, nothing to activate");
            return;
        }

        PoolString defaultName;
        const auto rootNode = CoreManager::GetConfigManager().GetConfig("GamesConfig"_intern);
        if (rootNode) {
            const auto defaultNode = rootNode.GetChild("defaultGame");
            if (defaultNode) {
                if (auto attr = defaultNode.GetAttribute("name")) {
                    defaultName = PoolString::Intern(*attr);
                }
            }
        }

        if (defaultName.Empty()) {
            defaultName = _games.front()->GetName();
            LOG_INFO(Format("GameManager: defaultGame not set, falling back to '{}'", defaultName.ToStringView()).c_str());
        }

        SwitchTo(defaultName);
    }

    void GameManager::Shutdown() {
        if (_active) {
            _active->Stop();
            _active.Reset();
        }
        _games.clear();
    }

    void GameManager::SwitchTo(PoolString name) {
        if (_active && _active->GetName() == name) {
            return;
        }

        auto next = FindGame(name);
        if (!next) {
            LOG_ERROR(Format("GameManager: no registered game named '{}'", name.ToStringView()).c_str());
            return;
        }

        if (_active) {
            _active->Stop();
        }

        const PoolString sceneName = next->GetSceneName();
        if (!sceneName.Empty()) {
            CoreManager::GetSceneManager().SetActiveScene(sceneName);
        }

        _active = std::move(next);
        _active->Start();

        LOG_INFO(Format("GameManager: switched to '{}'", _active->GetName().ToStringView()).c_str());
    }

    void GameManager::ResetActive() {
        if (_active) {
            _active->Reset();
        }
    }

    IntrusivePtr<IGame> GameManager::FindGame(PoolString name) const {
        for (const auto& game : _games) {
            if (game && game->GetName() == name) {
                return game;
            }
        }
        return {};
    }

}  // namespace BECore
