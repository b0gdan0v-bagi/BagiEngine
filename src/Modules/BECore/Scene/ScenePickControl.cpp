#include "ScenePickControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodePath.h>
#include <BECore/Scene/ScenePick.h>
#include <Generated/ScenePickControl.gen.hpp>

namespace BECore {

    namespace {

        /// A coordinate of a point of the screen as a fraction of that side; nullopt when absent, no finite number or off the screen
        Optional<float> GetScreenFraction(JsonView params, StringView key) {
            const Optional<double> value = GetParamNumber(params, key);
            if (!value || *value < 0.0 || *value > 1.0) {
                return {};
            }
            return static_cast<float>(*value);
        }

        /// Makes @p answer a hit of @p node, unless no path can name it: the root, and a node past the limits of a path
        void AnswerHit(const SceneNode& node, ScenePickAnswer& answer) {
            size_t depth = 0;
            for (const SceneNode* at = &node; at->GetParent() != nullptr; at = at->GetParent()) {
                ++depth;
            }
            if (depth == 0 || depth > SceneNodePathLimits::kMaxSegments) {
                return;
            }
            String path;
            AppendSceneNodePath(path, node);
            if (path.size() > SceneNodePathLimits::kMaxPathBytes) {
                return;
            }
            answer.hit = true;
            answer.node = std::move(path);
        }

        void HandleScenePick(ControlCall& call, SceneProvider& activeScene, SceneOnScreenProvider& sceneIsOnScreen) {
            const Optional<float> x = GetScreenFraction(call.Params(), "x");
            const Optional<float> y = GetScreenFraction(call.Params(), "y");
            if (!x || !y) {
                call.Fail(ControlErrors::kInvalidParams, "'x' and 'y' have to be numbers from 0 to 1, fractions of the screen");
                return;
            }
            Scene* const scene = activeScene ? activeScene() : nullptr;
            if (scene == nullptr) {
                call.Fail(SceneControlErrors::kNoScene, "there is no active scene");
                return;
            }
            if (!sceneIsOnScreen || !sceneIsOnScreen()) {
                call.Fail(ScenePickControlErrors::kNotScreenView, "the screen of this program is not the picture of the scene (it runs in the Editor mode)");
                return;
            }
            ScenePickAnswer answer;
            const Vec2 size = scene->GetDrawOutputSize();
            // A scene that never drew has no fit and no laid out canvas to pick in
            if (size.x > 0.0f && size.y > 0.0f) {
                const ScenePick::Hit hit = ScenePick::PickAt(*scene, *x * size.x, *y * size.y);
                if (hit.node != nullptr) {
                    AnswerHit(*hit.node, answer);
                }
            }
            call.ReplyObject(answer);
        }

    }  // namespace

    void RegisterScenePickControl(ControlService& service, SceneProvider activeScene, SceneOnScreenProvider sceneIsOnScreen) {
        service.RegisterHandler("scene_pick", [scene = std::move(activeScene), onScreen = std::move(sceneIsOnScreen)](ControlCall& call) mutable { HandleScenePick(call, scene, onScreen); });
        service.DescribeMethod(
            "scene_pick", {"Names the topmost node or canvas element's node of the active scene under a point of the screen, as a click in the editor's viewport picks",
                           {{"x", "number", true, "From 0 (left) to 1 (right) of the screen, and so of a screenshot of any size"}, {"y", "number", true, "From 0 (top) to 1 (bottom) of the screen"}}});
    }

}  // namespace BECore
