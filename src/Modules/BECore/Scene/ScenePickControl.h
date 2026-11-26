#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/SceneControl.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// Error codes of `scene_pick` beyond ControlErrors and SceneControlErrors
    namespace ScenePickControlErrors {
        inline constexpr StringView kNotScreenView = "not_screen_view";
    }  // namespace ScenePickControlErrors

    /// The answer of `scene_pick`
    struct ScenePickAnswer {
        BE_CLASS(ScenePickAnswer)

        BE_REFLECT_FIELD bool hit = false;
        /// The path of the picked node (SceneNodePath.h), empty on a miss
        BE_REFLECT_FIELD String node;
    };

    /// True while the program's screen (what `screenshot` captures) shows the scene of the SceneProvider, asked on every request
    using SceneOnScreenProvider = MoveOnlyFunction<bool()>;

    /**
     * @brief Registers the control API method that names the node under a point of the screen: `scene_pick`, by ScenePick::PickAt over the last Draw() of the scene.
     *
     * The point is in fractions of the screen, so a client that knows only a scaled screenshot can name it. Shapes and codes: `.claude/rules/control-scene-api.md`.
     * Registering twice replaces the handler.
     */
    void RegisterScenePickControl(ControlService& service, SceneProvider activeScene, SceneOnScreenProvider sceneIsOnScreen);

}  // namespace BECore
