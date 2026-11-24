#pragma once

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonViewOps.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>
#include <Widgets/ArtGen/ArtGenFileText.h>
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

    inline String StringOr(JsonView object, StringView key, StringView fallback = {}) {
        auto value = GetParamString(object, key);
        return value ? std::move(*value) : String(fallback);
    }

    /// True when @p object has the member and it is not null (a client may send an optional param as null)
    inline bool IsGiven(JsonView object, StringView key) {
        const Optional<JsonView> member = FindParam(object, key);
        return member && !member->IsNull();
    }

    /// The text of a JSON string value that is not a member of an object (an item of a list); nullopt for any other type
    inline Optional<String> AsString(JsonView value) {
        String text;
        if (!value.ReadText(text)) {
            return std::nullopt;
        }
        return text;
    }

    /// Wraps a handler of the session as a control method: a value is the reply, an error the failure. A handler answers the compact text of a
    /// document written by hand (a `String`, src/Widgets/ArtGen/CLAUDE.md, "The contract") or a reflected struct of ArtGenAnswers.h, which writes itself
    template <auto Method>
    void Register(ControlService& service, ArtGenSession& session, StringView name) {
        service.RegisterHandler(name, [&session](ControlCall& call) {
            auto result = (session.*Method)(call.Params());
            if (result) {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(*result)>, String>) {
                    call.Reply([&text = *result](JsonWriter& writer) { writer.Raw(text); });
                } else {
                    call.ReplyObject(*result);
                }
            } else {
                call.Fail(result.error());
            }
        });
    }

}  // namespace BECore::ArtGenControl
