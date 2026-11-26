#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ConfigManager;

    /// Error codes of `scene_save` beyond ControlErrors and SceneControlErrors
    namespace SceneSaveControlErrors {
        inline constexpr StringView kSaveFailed = "save_failed";
    }  // namespace SceneSaveControlErrors

    /// The answer of `scene_save`. `scene` looks at the pooled name of the scene
    struct SceneSaveAnswer {
        BE_CLASS(SceneSaveAnswer)

        /// The real file that was written: the content overlay's when one is set
        BE_REFLECT_FIELD String file;
        /// What the writer could not keep as it was, a line each (WriteSceneToConfig)
        BE_REFLECT_FIELD Vector<String> notes;
        /// False when the file lives only as long as the program does: a web page keeps it in its memory
        BE_REFLECT_FIELD bool persistent = true;
        BE_REFLECT_FIELD TextView scene;
    };

    /// The manager whose `SceneConfig` the method saves into, asked on every request; null while there is none
    using ConfigManagerProvider = MoveOnlyFunction<ConfigManager*()>;

    /**
     * @brief Registers the control API method that saves a scene: `scene_save`, the scene of @p activeScene into its `<scene>` of `SceneConfig` by SaveSceneToConfig, as the editor's Save writes it.
     *
     * After the write it sends SceneEvents::SceneSavedEvent. Shapes and codes: `.claude/rules/control-scene-api.md`. Registering twice replaces the handler.
     */
    void RegisterSceneSaveControl(ControlService& service, SceneProvider activeScene, ConfigManagerProvider configs);

}  // namespace BECore
