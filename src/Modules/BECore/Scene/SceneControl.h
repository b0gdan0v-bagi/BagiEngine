#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <cstddef>

namespace BECore {

    class ControlService;
    class Scene;
    class SceneNode;

    /// Error codes of the scene methods (the common ones are in ControlErrors, the ones of a field access in PropertyErrorCode)
    namespace SceneControlErrors {
        inline constexpr StringView kNoScene = "no_scene";
        inline constexpr StringView kNodeNotFound = "node_not_found";
        inline constexpr StringView kComponentNotFound = "component_not_found";
    }  // namespace SceneControlErrors

    namespace SceneControlLimits {
        /// Nodes `scene_tree` lists when the client names no limit
        inline constexpr size_t kDefaultTreeNodes = 500;
        /// The largest `limit` of `scene_tree`
        inline constexpr size_t kMaxTreeNodes = 5000;
    }  // namespace SceneControlLimits

    /// The scene the methods work on, asked on every request; null while there is none
    using SceneProvider = MoveOnlyFunction<Scene*()>;

    /**
     * @brief Registers the control API methods that reach the nodes of a scene by path (SceneNodePath.h): `scene_tree`, `scene_get` and `scene_set`.
     *
     * Shapes and codes: `.claude/rules/control-scene-api.md`. Registering twice replaces the handlers.
     */
    void RegisterSceneControl(ControlService& service, SceneProvider activeScene);

    /**
     * @brief Says that the control call of @p service running now touched @p node, or a component (its type name) and a field of it.
     *
     * For a method of a game that changes or acts on a node, before its answer: the editor shows the user where the client was. The rules are those of
     * ControlService::ReportTouch, so outside a call and for a call with no client nothing is recorded; a change made after the call takes the
     * overload of ReportTouch() that names the client, with SceneNode::GetRuntimeId().
     */
    void ReportNodeTouched(ControlService& service, const SceneNode& node, StringView component = {}, StringView field = {});

    /// ReportNodeTouched() over the service of CoreManager
    void ReportNodeTouched(const SceneNode& node, StringView component = {}, StringView field = {});

}  // namespace BECore
