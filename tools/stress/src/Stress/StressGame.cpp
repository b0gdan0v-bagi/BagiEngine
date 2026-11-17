#include "StressGame.h"

#include <BECore/Application/CommandLine.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneManager.h>
#include <Games/GameManager.h>
#include <Generated/StressGame.gen.hpp>
#include <Stress/RampController.h>
#include <Stress/StressNames.h>

namespace BECore::Stress {

    namespace {

        /// The scene that shows @p scene (a scene of tools/stress/config/SceneConfig.xml)
        PoolString SceneNameOf(StressScene scene) {
            switch (scene) {
                case StressScene::Sprites:
                    return StressNames::sprites;
                case StressScene::Entities:
                    return StressNames::entities;
                case StressScene::Ui:
                    return StressNames::ui;
            }
            return StressNames::sprites;
        }

        /// The settings of the command line; a bad flag is logged once and the defaults stand
        StressSettings LoadSettings() {
            auto settings = ParseStressSettings(CoreManager::GetCommandLine());
            if (!settings) {
                LOG_WARNING(Format("Stress: {}; starting with the defaults", settings.error()));
                return StressSettings{};
            }
            if (settings->ramp) {
                settings->count = kRampStart;  // the ramp starts at its own count, so a big --stress-count is never built first
            }
            return *settings;
        }

    }  // namespace

    StressGame::StressGame() : _settings(LoadSettings()), _state(_settings), _control(_state, [this] { ShowActiveScene(); }) {}

    IControllableGame* StressGame::AsControllable() {
        return &_control;
    }

    PoolString StressGame::GetName() const {
        return StressNames::game;
    }

    PoolString StressGame::GetSceneName() const {
        return SceneNameOf(_state.GetScene());
    }

    void StressGame::SetObjectCount(uint32_t count) {
        _state.SetCount(count);
        SyncStateVersion();
    }

    String StressGame::Describe() const {
        return Format("scene={} count={} language={}", ToFlagValue(_state.GetScene()), _state.GetCount(), ToFlagValue(_state.GetLanguage()));
    }

    void StressGame::SyncStateVersion() {
        _stateVersion = _state.Version();
    }

    void StressGame::ShowActiveScene() {
        // No scene manager before the start-up and in a test that has none: the scene is shown at the next start then
        if (SceneManager* manager = CoreManager::FindSceneManager()) {
            manager->SetActiveScene(GetSceneName());
        }
    }

    StressGame* StressGame::GetActive() {
        IGame* game = GameManager::GetInstance().GetActive();
        return game != nullptr ? game->Cast<StressGame>() : nullptr;
    }

}  // namespace BECore::Stress
