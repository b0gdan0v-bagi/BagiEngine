#pragma once

#include <BECore/Time/GamePause.h>  // kDefaultStepSeconds
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    /// Why a control request failed: a stable code for programs and a message for people
    struct ControlError {
        String code;
        String message;
    };

    /// Receives the serialized JSON text of a response. Called on the main thread, except for a request posted after
    /// ControlService::Shutdown() (answered on the posting thread).
    using ControlReplyFn = MoveOnlyFunction<void(String)>;

    /// What the transport is doing, for whoever shows it (the editor's status bar). Application publishes it every frame, so a widget
    /// does not reach into the transport
    struct ControlServerStatus {
        bool listening = false;
        uint16_t port = 0;
        uint32_t clients = 0;  // open connections, handshakes done

        [[nodiscard]] bool operator==(const ControlServerStatus& other) const = default;
    };

    /// Error codes of the control protocol. They are part of the protocol: clients match on them.
    namespace ControlErrors {
        inline constexpr StringView kParseError = "parse_error";
        inline constexpr StringView kInvalidRequest = "invalid_request";
        inline constexpr StringView kInvalidParams = "invalid_params";
        inline constexpr StringView kUnknownMethod = "unknown_method";
        inline constexpr StringView kRequestTooLarge = "request_too_large";
        inline constexpr StringView kInternalError = "internal_error";
        inline constexpr StringView kShuttingDown = "shutting_down";
        inline constexpr StringView kManualClock = "manual_clock";
    }  // namespace ControlErrors

    [[nodiscard]] inline ControlError MakeControlError(StringView code, String message) {
        return ControlError{String(code), std::move(message)};
    }

}  // namespace BECore
