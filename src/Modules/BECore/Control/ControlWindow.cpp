#include "ControlWindow.h"

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UISettings.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kSetWindowMethod = "set_window";

        /// The insets of @p safe inside a window of @p width by @p height window units; rounding never makes one negative
        WindowInsets InsetsOf(const Rect& safe, int width, int height) {
            WindowInsets insets;
            insets.left = std::max(0.0f, safe.x);
            insets.top = std::max(0.0f, safe.y);
            insets.right = std::max(0.0f, static_cast<float>(width) - (safe.x + safe.w));
            insets.bottom = std::max(0.0f, static_cast<float>(height) - (safe.y + safe.h));
            return insets;
        }

        Json DescribeInsets(const WindowInsets& insets) {
            Json safeArea = Json::object();
            safeArea["top"] = insets.top;
            safeArea["right"] = insets.right;
            safeArea["bottom"] = insets.bottom;
            safeArea["left"] = insets.left;
            return safeArea;
        }

        ControlError InvalidParams(String message) {
            return MakeControlError(ControlErrors::kInvalidParams, std::move(message));
        }

        /// A required integer; one that is out of the range of int is passed on as the nearest int, so the limits check that follows names the field
        std::expected<int, ControlError> ReadSize(const Json& params, StringView key) {
            const auto value = GetJsonInt(params, key);
            if (!value) {
                return std::unexpected(InvalidParams(Format("'{}' is required and has to be an integer of window units", key)));
            }
            constexpr int64_t kLowest = std::numeric_limits<int>::lowest();
            constexpr int64_t kHighest = std::numeric_limits<int>::max();
            return static_cast<int>(std::clamp(*value, kLowest, kHighest));
        }

        /// An optional number: @p fallback when it is absent, invalid_params when it is there and is not a number
        std::expected<float, ControlError> ReadOptionalNumber(const Json& object, StringView key, float fallback, StringView where) {
            if (FindJsonMember(object, key) == nullptr) {
                return fallback;
            }
            const auto value = GetJsonNumber(object, key);
            if (!value) {
                return std::unexpected(InvalidParams(Format("'{}{}' has to be a number", where, key)));
            }
            return static_cast<float>(*value);
        }

        /// The `safeArea` of the request over @p current: absent keeps it, a present one needs all four members
        std::expected<WindowInsets, ControlError> ReadSafeArea(const Json& params, const WindowInsets& current) {
            const Json* member = FindJsonMember(params, "safeArea");
            if (member == nullptr) {
                return current;
            }
            if (!member->is_object()) {
                return std::unexpected(InvalidParams("'safeArea' has to be an object {top, right, bottom, left} of window units"));
            }
            WindowInsets insets;
            struct Edge {
                StringView name;
                float* target;
            };
            for (const Edge& edge : {Edge{"top", &insets.top}, Edge{"right", &insets.right}, Edge{"bottom", &insets.bottom}, Edge{"left", &insets.left}}) {
                if (FindJsonMember(*member, edge.name) == nullptr) {
                    return std::unexpected(InvalidParams(Format("'safeArea' needs all of top, right, bottom and left; '{}' is missing", edge.name)));
                }
                const auto value = ReadOptionalNumber(*member, edge.name, 0.0f, "safeArea.");
                if (!value) {
                    return std::unexpected(value.error());
                }
                *edge.target = *value;
            }
            return insets;
        }

        /// What a set_window request asks for, merged with the current window
        struct WindowRequest {
            WindowSimulation simulation;
            float userScale = 1.0f;
        };

        std::expected<WindowRequest, ControlError> ReadWindowRequest(const Json& params, const IMainWindow& window) {
            WindowRequest request;
            const auto width = ReadSize(params, "width");
            if (!width) {
                return std::unexpected(width.error());
            }
            const auto height = ReadSize(params, "height");
            if (!height) {
                return std::unexpected(height.error());
            }
            request.simulation.width = *width;
            request.simulation.height = *height;

            const auto density = ReadOptionalNumber(params, "density", window.GetPixelDensity(), "");
            if (!density) {
                return std::unexpected(density.error());
            }
            request.simulation.density = *density;

            const auto userScale = ReadOptionalNumber(params, "userScale", CoreManager::GetUIEnvironment().GetSettings().userScale, "");
            if (!userScale) {
                return std::unexpected(userScale.error());
            }
            request.userScale = *userScale;

            const auto safeArea = ReadSafeArea(params, InsetsOf(window.GetSafeArea(), window.GetWidth(), window.GetHeight()));
            if (!safeArea) {
                return std::unexpected(safeArea.error());
            }
            request.simulation.safeArea = *safeArea;
            return request;
        }

        std::expected<void, ControlError> CheckRequest(const WindowRequest& request) {
            if (const auto valid = ValidateWindowSimulation(request.simulation); !valid) {
                return std::unexpected(InvalidParams(valid.error()));
            }
            // Written so that a NaN fails too
            if (!(request.userScale >= UISettings::kMinUserScale && request.userScale <= UISettings::kMaxUserScale)) {
                return std::unexpected(InvalidParams(Format("userScale must be a number from {} to {}, got {}", UISettings::kMinUserScale, UISettings::kMaxUserScale, request.userScale)));
            }
            return {};
        }

        void HandleSetWindow(ControlCall& call) {
            const IntrusivePtr<IMainWindow>& window = CoreManager::GetMainWindow();
            if (!window) {
                call.Fail(WindowControlErrors::kNotHeadless, "there is no window to simulate");
                return;
            }
            if (!window->IsHeadless()) {
                call.Fail(WindowControlErrors::kNotHeadless,
                          "this is a real window: its size, density and safe area belong to the OS and the person, so only a headless game (--headless) can be given another");
                return;
            }

            const auto request = ReadWindowRequest(call.Params(), *window);
            if (!request) {
                call.Fail(request.error());
                return;
            }
            if (const auto checked = CheckRequest(*request); !checked) {
                call.Fail(checked.error());
                return;
            }

            // The window first: it is the part that can still refuse, and the user scale must not change when it does
            if (const auto simulated = window->Simulate(request->simulation); !simulated) {
                call.Fail(ControlErrors::kInvalidParams, simulated.error());
                return;
            }
            CoreManager::GetUIEnvironment().SetUserScale(request->userScale);

            // One frame lays the scene out for the new size, so what the client asks next is of it
            call.DeferFrames(1, 0.0, [](ControlCall& done) { done.Reply(DescribeWindowStatus()); });
        }

    }  // namespace

    Json DescribeWindowStatus() {
        const IntrusivePtr<IMainWindow>& window = CoreManager::GetMainWindow();
        const IntrusivePtr<IRenderer>& renderer = CoreManager::GetRenderer();

        Json result = Json::object();
        const int width = window ? window->GetWidth() : 0;
        const int height = window ? window->GetHeight() : 0;
        result["width"] = width;
        result["height"] = height;
        result["density"] = window ? window->GetPixelDensity() : 1.0f;
        result["displayScale"] = window ? window->GetDisplayScale() : 1.0f;
        result["safeArea"] = DescribeInsets(window ? InsetsOf(window->GetSafeArea(), width, height) : WindowInsets{});
        result["outputWidth"] = renderer ? static_cast<int>(std::lround(renderer->GetOutputWidth())) : 0;
        result["outputHeight"] = renderer ? static_cast<int>(std::lround(renderer->GetOutputHeight())) : 0;
        result["userScale"] = CoreManager::GetUIEnvironment().GetSettings().userScale;
        result["headless"] = window && window->IsHeadless();
        return result;
    }

    void RegisterWindowControl(ControlService& service) {
        service.RegisterHandler(kSetWindowMethod, HandleSetWindow);
        service.DescribeMethod(
            kSetWindowMethod,
            {"Gives a headless game another window: size, pixel density, user scale and safe area. Answers after one frame with the `window` object of `status`; nothing changes unless every value is "
             "valid. "
             "Refused with `not_headless` for a real window. Left-out optional values keep their current value",
             {{"width", "integer", true, Format("Window units, {} to {}", WindowLimits::kMinSize, WindowLimits::kMaxSize)},
              {"height", "integer", true, Format("Window units, {} to {}", WindowLimits::kMinSize, WindowLimits::kMaxSize)},
              {"density", "number", false,
               Format("Output pixels per window unit, {} to {}; the output (screenshot, renderer) is size times density, each side at most {} pixels", WindowLimits::kMinDensity,
                      WindowLimits::kMaxDensity, WindowLimits::kMaxOutputSide)},
              {"userScale", "number", false, Format("The accessibility scale of the UI, {} to {}; never clamped, out of range is an error", UISettings::kMinUserScale, UISettings::kMaxUserScale)},
              {"safeArea", "object", false,
               "{top, right, bottom, left} in window units, all four required, each not negative; opposite sides must leave room (left + right < width, top + bottom < height); all zeros clears "
               "it"}}});
    }

}  // namespace BECore
