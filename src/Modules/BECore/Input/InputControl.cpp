#include "InputControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UIControl.h>
#include <SDL3/SDL_mouse.h>
#include <cmath>

namespace BECore {

    namespace {

        /// Far beyond any screen; keeps a double from overflowing the float of the event
        constexpr double kMaxClickCoordinate = 1.0e6;

        constexpr StringView kSpaceOutput = "output";
        constexpr StringView kSpaceScene = "scene";

        SDL_MouseButtonEvent MakeButtonEvent(float x, float y, uint8_t button, uint32_t which, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.which = which;
            event.button = button;
            event.down = down;
            event.clicks = 1;
            event.x = x;
            event.y = y;
            return event;
        }

        Optional<float> ReadCoordinate(const Json& params, StringView key) {
            const auto number = GetJsonNumber(params, key);
            if (!number || !std::isfinite(*number) || std::fabs(*number) > kMaxClickCoordinate) {
                return {};
            }
            return static_cast<float>(*number);
        }

        Optional<uint8_t> ReadButton(const Json& params) {
            if (FindJsonMember(params, "button") == nullptr) {
                return static_cast<uint8_t>(SDL_BUTTON_LEFT);
            }
            const auto text = GetJsonString(params, "button");
            if (!text) {
                return {};
            }
            if (*text == "left") {
                return static_cast<uint8_t>(SDL_BUTTON_LEFT);
            }
            if (*text == "right") {
                return static_cast<uint8_t>(SDL_BUTTON_RIGHT);
            }
            if (*text == "middle") {
                return static_cast<uint8_t>(SDL_BUTTON_MIDDLE);
            }
            return {};
        }

        Optional<uint32_t> ReadWhich(const Json& params) {
            if (FindJsonMember(params, "which") == nullptr) {
                return kControlPointerId;
            }
            const auto value = GetJsonInt(params, "which");
            if (!value || *value < 0 || *value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                return {};
            }
            return static_cast<uint32_t>(*value);
        }

        void HandleClickAt(ControlCall& call) {
            const Json& params = call.Params();
            const auto x = ReadCoordinate(params, "x");
            const auto y = ReadCoordinate(params, "y");
            if (!x || !y) {
                call.Fail(ControlErrors::kInvalidParams, "'x' and 'y' have to be finite numbers");
                return;
            }
            bool sceneSpace = false;
            if (FindJsonMember(params, "space") != nullptr) {
                const auto space = GetJsonString(params, "space");
                if (!space || (*space != kSpaceOutput && *space != kSpaceScene)) {
                    call.Fail(ControlErrors::kInvalidParams, "'space' has to be \"output\" or \"scene\"");
                    return;
                }
                sceneSpace = *space == kSpaceScene;
            }
            const auto button = ReadButton(params);
            if (!button) {
                call.Fail(ControlErrors::kInvalidParams, "'button' has to be \"left\", \"right\" or \"middle\"");
                return;
            }
            const auto which = ReadWhich(params);
            if (!which) {
                call.Fail(ControlErrors::kInvalidParams, "'which' has to be a pointer id, an integer from 0 to 4294967295");
                return;
            }

            float outputX = *x;
            float outputY = *y;
            if (sceneSpace) {
                const Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
                if (scene == nullptr) {
                    call.Fail(UIControlErrors::kNoScene, "there is no active scene");
                    return;
                }
                const Rect output = scene->GetViewTransform().ToOutput(*x, *y, 0.0f, 0.0f);
                outputX = output.x;
                outputY = output.y;
            }

            const PointClick click = ClickOutputPoint(CoreManager::GetUIInput(), outputX, outputY, *button, *which);

            Json result = Json::object();
            result["x"] = *x;
            result["y"] = *y;
            result["outputX"] = outputX;
            result["outputY"] = outputY;
            result["consumed"] = click.consumed;
            call.Reply(std::move(result));
        }

    }  // namespace

    PointClick ClickOutputPoint(UIInput& input, float x, float y, uint8_t button, uint32_t which) {
        SDL_MouseMotionEvent motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.which = which;
        motion.x = x;
        motion.y = y;
        static_cast<void>(input.DispatchMouseMotion(motion));
        PointClick click;
        click.consumed = input.DispatchMouseButtonDown(MakeButtonEvent(x, y, button, which, true));
        click.taker = input.LastPressTaker();
        static_cast<void>(input.DispatchMouseButtonUp(MakeButtonEvent(x, y, button, which, false)));
        return click;
    }

    void RegisterInputControl(ControlService& service) {
        service.RegisterHandler("click_at", HandleClickAt);
        service.DescribeMethod("click_at", {"Clicks at a point as a person does, with no element name: a scene target or a canvas under the point takes it",
                                            {{"x", "number", true, "The horizontal position"},
                                             {"y", "number", true, "The vertical position"},
                                             {"space", "string", false, "\"output\" (default): render output pixels; \"scene\": scene units of the active scene"},
                                             {"button", "string", false, "\"left\" (default), \"right\" or \"middle\""},
                                             {"which", "number", false, "The pointer id, default 0"}}});
    }

}  // namespace BECore
