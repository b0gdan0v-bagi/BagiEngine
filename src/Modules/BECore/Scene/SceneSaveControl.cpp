#include "SceneSaveControl.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneConfigWriter.h>
#include <Events/SceneEvents.h>
#include <Generated/SceneSaveControl.gen.hpp>

namespace BECore {

    namespace {

#if PLATFORM_WEB
        constexpr bool kSavedFilesPersist = false;
#else
        constexpr bool kSavedFilesPersist = true;
#endif

        void HandleSceneSave(ControlCall& call, SceneProvider& activeScene, ConfigManagerProvider& configManager) {
            Scene* const scene = activeScene ? activeScene() : nullptr;
            if (scene == nullptr) {
                call.Fail(SceneControlErrors::kNoScene, "there is no active scene");
                return;
            }
            ConfigManager* const configs = configManager ? configManager() : nullptr;
            if (configs == nullptr) {
                call.Fail(SceneSaveControlErrors::kSaveFailed, "there is no config manager");
                return;
            }
            auto notes = SaveSceneToConfig(*configs, *scene);
            if (!notes) {
                call.Fail(SceneSaveControlErrors::kSaveFailed, notes.error());
                return;
            }
            SceneSaveAnswer answer;
            // After the save: with a content overlay the config has moved to the copy
            answer.file = String(configs->GetConfigFilePath(ConfigNames::scene).View());
            answer.notes = std::move(*notes);
            answer.persistent = kSavedFilesPersist;
            answer.scene = TextView(scene->GetName().ToStringView());
            SceneEvents::SceneSavedEvent::Emit(scene);
            call.ReplyObject(answer);
        }

    }  // namespace

    void RegisterSceneSaveControl(ControlService& service, SceneProvider activeScene, ConfigManagerProvider configs) {
        service.RegisterHandler("scene_save", [scene = std::move(activeScene), manager = std::move(configs)](ControlCall& call) mutable { HandleSceneSave(call, scene, manager); });
        service.DescribeMethod("scene_save", {"Saves the active scene into its SceneConfig file, as the Save of the program's editor does; node files are not written", {}});
    }

}  // namespace BECore
