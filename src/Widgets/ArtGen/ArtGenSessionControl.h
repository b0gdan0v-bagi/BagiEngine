#pragma once

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <expected>
#include <type_traits>

namespace BECore::ArtGenControl {

    // The error codes of the art generation methods (src/Widgets/ArtGen/CLAUDE.md)
    inline constexpr StringView kBusyCode = "artgen_busy";
    inline constexpr StringView kNoRunCode = "artgen_no_run";
    inline constexpr StringView kNoVariantCode = "artgen_no_variant";
    inline constexpr StringView kAcceptFailedCode = "artgen_accept_failed";
    inline constexpr StringView kNoSetupCode = "artgen_no_setup";
    inline constexpr StringView kNoSetCode = "artgen_no_set";
    inline constexpr StringView kSetIncompleteCode = "artgen_set_incomplete";

    inline ControlError InvalidParams(StringView message) {
        return ControlError{String(ControlErrors::kInvalidParams), String(message)};
    }

    inline ControlError MakeError(StringView code, String message) {
        return ControlError{String(code), std::move(message)};
    }

    inline String StringOr(const Json& object, StringView key, StringView fallback = {}) {
        const auto value = GetJsonString(object, key);
        return value ? *value : String(fallback);
    }

    inline Json ToJsonString(const String& text) {
        return MakeJsonString(StringView(text.data(), text.size()));
    }

    /// True when @p object has the member and it is not null (a client may send an optional param as null)
    inline bool IsGiven(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        return member != nullptr && !member->is_null();
    }

    /// The text of a JSON string value that is not a member of an object (an item of a list); nullopt for any other type
    inline Optional<String> AsString(const Json& value) {
        Json holder = Json::object();
        holder["value"] = value;
        return GetJsonString(holder, "value");
    }

    /// Wraps a handler of the session as a control method: a value is the reply, an error the failure. A handler answers a `Json` document (a payload that stays by hand,
    /// src/Widgets/ArtGen/CLAUDE.md, "The contract") or a reflected struct of ArtGenAnswers.h, which writes itself
    template <auto Method>
    void Register(ControlService& service, ArtGenSession& session, StringView name) {
        service.RegisterHandler(name, [&session](ControlCall& call) {
            auto result = (session.*Method)(call.Params());
            if (result) {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(*result)>, Json>) {
                    call.Reply(std::move(*result));
                } else {
                    call.ReplyObject(*result);
                }
            } else {
                call.Fail(result.error());
            }
        });
    }

}  // namespace BECore::ArtGenControl
