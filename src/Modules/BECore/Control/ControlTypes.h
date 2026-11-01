#pragma once

namespace BECore {

    /// Why a control request failed: a stable code for programs and a message for people
    struct ControlError {
        eastl::string code;
        eastl::string message;
    };

    /// Receives the serialized JSON text of a response. Called on the main thread, except for a request posted after
    /// ControlService::Shutdown() (answered on the posting thread).
    using ControlReplyFn = std::function<void(eastl::string)>;

    /// Frame time of a frame that a deferred call asks for when it does not say how long the frame is: one frame at 60 fps
    inline constexpr double kDefaultStepSeconds = 1.0 / 60.0;

    /// Error codes of the control protocol. They are part of the protocol: clients match on them.
    namespace ControlErrors {
        inline constexpr eastl::string_view kParseError = "parse_error";
        inline constexpr eastl::string_view kInvalidRequest = "invalid_request";
        inline constexpr eastl::string_view kInvalidParams = "invalid_params";
        inline constexpr eastl::string_view kUnknownMethod = "unknown_method";
        inline constexpr eastl::string_view kRequestTooLarge = "request_too_large";
        inline constexpr eastl::string_view kInternalError = "internal_error";
        inline constexpr eastl::string_view kShuttingDown = "shutting_down";
    }  // namespace ControlErrors

    [[nodiscard]] inline ControlError MakeControlError(eastl::string_view code, eastl::string message) {
        return ControlError{eastl::string(code), std::move(message)};
    }

}  // namespace BECore
