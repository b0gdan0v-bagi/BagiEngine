#include "InputControl.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/InputAnswers.h>
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

        /// A wheel amount: absent is 0, present has to be finite and not beyond the click range
        Optional<float> ReadWheelAmount(const Json& params, StringView key) {
            if (FindJsonMember(params, key) == nullptr) {
                return 0.0f;
            }
            return ReadCoordinate(params, key);
        }

        void HandleWheelAt(ControlCall& call) {
            const Json& params = call.Params();
            const auto x = ReadCoordinate(params, "x");
            const auto y = ReadCoordinate(params, "y");
            if (!x || !y) {
                call.Fail(ControlErrors::kInvalidParams, "'x' and 'y' have to be finite numbers");
                return;
            }
            const auto dx = ReadWheelAmount(params, "dx");
            const auto dy = ReadWheelAmount(params, "dy");
            if (!dx || !dy) {
                call.Fail(ControlErrors::kInvalidParams, "'dx' and 'dy' have to be finite numbers");
                return;
            }
            if (FindJsonMember(params, "dx") == nullptr && FindJsonMember(params, "dy") == nullptr) {
                call.Fail(ControlErrors::kInvalidParams, "'dx' or 'dy' is needed: a wheel of nothing is a mistake");
                return;
            }
            bool flipped = false;
            if (FindJsonMember(params, "flipped") != nullptr) {
                const auto value = GetJsonBool(params, "flipped");
                if (!value) {
                    call.Fail(ControlErrors::kInvalidParams, "'flipped' has to be true or false");
                    return;
                }
                flipped = *value;
            }
            const auto which = ReadWhich(params);
            if (!which) {
                call.Fail(ControlErrors::kInvalidParams, "'which' has to be a pointer id, an integer from 0 to 4294967295");
                return;
            }

            InputWheelAnswer answer;
            answer.consumed = WheelOutputPoint(CoreManager::GetUIInput(), *x, *y, *dx, *dy, flipped, *which);
            answer.x = static_cast<double>(*x);
            answer.y = static_cast<double>(*y);
            call.ReplyObject(answer);
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

            InputClickAnswer answer;
            answer.consumed = click.consumed;
            answer.outputX = static_cast<double>(outputX);
            answer.outputY = static_cast<double>(outputY);
            answer.x = static_cast<double>(*x);
            answer.y = static_cast<double>(*y);
            call.ReplyObject(answer);
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

    bool WheelOutputPoint(UIInput& input, float x, float y, float dx, float dy, bool flipped, uint32_t which) {
        SDL_MouseWheelEvent event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.which = which;
        event.x = dx;
        event.y = dy;
        event.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
        event.mouse_x = x;
        event.mouse_y = y;
        return input.DispatchMouseWheel(event);
    }

    void RegisterInputControl(ControlService& service) {
        service.RegisterHandler("click_at", HandleClickAt);
        service.DescribeMethod("click_at", {"Clicks at a point as a person does, with no element name: a scene target or a canvas under the point takes it",
                                            {{"x", "number", true, "The horizontal position"},
                                             {"y", "number", true, "The vertical position"},
                                             {"space", "string", false, "\"output\" (default): render output pixels; \"scene\": scene units of the active scene"},
                                             {"button", "string", false, "\"left\" (default), \"right\" or \"middle\""},
                                             {"which", "number", false, "The pointer id, default 0"}}});
        service.RegisterHandler("wheel_at", HandleWheelAt);
        service.DescribeMethod("wheel_at", {"Turns the mouse wheel at a point in output pixels: a scrollable document or a canvas under the point may take it",
                                            {{"x", "number", true, "The horizontal position, output pixels"},
                                             {"y", "number", true, "The vertical position, output pixels"},
                                             {"dx", "number", false, "The horizontal amount as SDL gives it, positive to the right; dx or dy is needed"},
                                             {"dy", "number", false, "The vertical amount as SDL gives it, positive away from the user (up); dx or dy is needed"},
                                             {"flipped", "boolean", false, "true: the event says SDL_MOUSEWHEEL_FLIPPED (the values are opposite)"},
                                             {"which", "number", false, "The pointer id, default 0"}}});
    }

}  // namespace BECore
