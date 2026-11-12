#pragma once

#include <BECore/Time/GamePause.h>  // kDefaultStepSeconds: the frame time of a deferred call that does not say how long its frame is

namespace BECore {

    /// Why a control request failed: a stable code for programs and a message for people
    struct ControlError {
        eastl::string code;
        eastl::string message;
    };

    /// Receives the serialized JSON text of a response. Called on the main thread, except for a request posted after
    /// ControlService::Shutdown() (answered on the posting thread).
    using ControlReplyFn = std::function<void(eastl::string)>;

    /// What the transport of the control API is doing, for whoever shows it (the editor's status bar). Published by Application every
    /// frame, so a widget reads it without reaching into the transport
    struct ControlServerStatus {
        bool listening = false;
        uint16_t port = 0;
        uint32_t clients = 0;  // open connections, handshakes done

        [[nodiscard]] bool operator==(const ControlServerStatus& other) const = default;
    };

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
