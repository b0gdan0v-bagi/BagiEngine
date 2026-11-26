#include "SceneNodeControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodePath.h>
#include <Events/SceneEvents.h>
#include <Generated/SceneNodeControl.gen.hpp>

namespace BECore {

    namespace {

        void HandleSceneSetNode(ControlCall& call, SceneProvider& activeScene) {
            const auto path = RequireParamString(call.Params(), "node");
            if (!path) {
                call.Fail(path.error());
                return;
            }
            constexpr int64_t kLowest = std::numeric_limits<int32_t>::min();
            constexpr int64_t kHighest = std::numeric_limits<int32_t>::max();
            const Optional<int64_t> layer = GetParamInt(call.Params(), "layer");
            if (!layer || *layer < kLowest || *layer > kHighest) {
                call.Fail(ControlErrors::kInvalidParams, Format("'layer' has to be an integer from {} to {}", kLowest, kHighest));
                return;
            }
            Scene* const scene = activeScene ? activeScene() : nullptr;
            if (scene == nullptr) {
                call.Fail(SceneControlErrors::kNoScene, "there is no active scene");
                return;
            }
            const auto node = FindSceneNodeByPath(scene->GetRootNode(), *path);
            if (!node) {
                call.Fail(ControlErrors::kInvalidParams, Format("'node': {}", node.error()));
                return;
            }
            if (*node == nullptr) {
                call.Fail(SceneControlErrors::kNodeNotFound, Format("there is no node '{}' in scene '{}'", ShownClientText(*path), scene->GetName().ToStringView()));
                return;
            }
            // Not a write of the reflected field: only SetLayer() tells the scene to order its draw list again
            (*node)->SetLayer(static_cast<int32_t>(*layer));
            SceneEvents::SceneNodeEditedEvent::Emit(scene, *node);
            SceneSetNodeAnswer answer;
            answer.layer = (*node)->GetLayer();
            answer.node = TextView(StringView(*path));
            call.ReplyObject(answer);
        }

    }  // namespace

    void RegisterSceneNodeControl(ControlService& service, SceneProvider activeScene) {
        service.RegisterHandler("scene_set_node", [scene = std::move(activeScene)](ControlCall& call) mutable { HandleSceneSetNode(call, scene); });
        service.DescribeMethod("scene_set_node", {"Sets the draw layer of a node of the active scene, as an edit in the inspector does",
                                                  {{"node", "string", true, "The path of the node, as in scene_get"}, {"layer", "integer", true, "The draw layer: a lower one is drawn first"}}});
    }

}  // namespace BECore
