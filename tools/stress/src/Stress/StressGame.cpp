#include "StressGame.h"

#include <BECore/GameManager/CoreManager.h>
#include <Games/GameManager.h>
#include <Generated/StressGame.gen.hpp>

namespace BECore::Stress {

    namespace {

        /// The scene that shows @p scene (a scene of tools/stress/config/SceneConfig.xml)
        PoolString SceneNameOf(StressScene scene) {
            switch (scene) {
                case StressScene::Sprites:
                    return "StressSprites"_intern;
                case StressScene::Entities:
                    return "StressEntities"_intern;
                case StressScene::Ui:
                    return "StressUi"_intern;
            }
            return "StressSprites"_intern;
        }

    }  // namespace

    StressGame::StressGame() {
        const auto settings = ParseStressSettings(CoreManager::GetCommandLine());
        if (settings) {
            _settings = *settings;
        } else {
            LOG_WARNING(Format("Stress: {}; starting with the defaults", settings.error()));
        }
        _state = StressState(_settings);
    }

    PoolString StressGame::GetName() const {
        return "Stress"_intern;
    }

    PoolString StressGame::GetSceneName() const {
        return SceneNameOf(_state.GetScene());
    }

    StressGame* StressGame::GetActive() {
        IGame* game = GameManager::GetInstance().GetActive();
        return game != nullptr ? game->Cast<StressGame>() : nullptr;
    }

}  // namespace BECore::Stress
