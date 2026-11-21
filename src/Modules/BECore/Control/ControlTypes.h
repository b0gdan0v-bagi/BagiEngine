#pragma once

#include <BECore/Control/JsonFwd.h>
#include <BECore/Time/GamePause.h>  // kDefaultStepSeconds
#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <expected>

namespace BECore {

    /// Why a control request failed: a stable code for programs and a message for people
    struct ControlError {
        String code;
        String message;
    };

    /// What a call that ran inside another call answered (ControlService::RunNested): the result, or the error
    using ControlAnswer = std::expected<Json, ControlError>;

    /// Receives the serialized JSON text of a response. Called on the main thread, except for a request posted after
    /// ControlService::Shutdown() (answered on the posting thread). The text lives in a buffer of the service that the next
    /// response reuses: it is valid for the call only, so a transport sends or copies it before it returns.
    using ControlReplyFn = MoveOnlyFunction<void(const String&)>;

    /// What the transport is doing, for whoever shows it (the editor's status bar). Application publishes it every frame, so a widget
    /// does not reach into the transport
    struct ControlServerStatus {
        bool listening = false;
        uint16_t port = 0;
        uint32_t clients = 0;  // open connections, handshakes done

        [[nodiscard]] bool operator==(const ControlServerStatus& other) const = default;
    };

    /// One parameter of a control method as its module describes it. For people and for the completion of a console: nothing checks it against the handler,
    /// which stays the judge of its parameters
    struct ControlParamInfo {
        String name;
        String type;  // "string", "integer", "number", "boolean", "object", "array" or "any"; free text for people
        bool required = false;
        String description;
    };

    /// What a module says about a control method (ControlService::DescribeMethod). For people and for completion, never checked against the handler
    struct ControlMethodInfo {
        String summary;  // one line
        Vector<ControlParamInfo> params;
    };

    /// A registered method and its description, if its module gave one (ControlService::GetMethods)
    struct ControlMethodListing {
        String name;
        const ControlMethodInfo* info = nullptr;  // null without a description
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
