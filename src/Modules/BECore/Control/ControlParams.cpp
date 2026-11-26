#include "ControlParams.h"

#include <cmath>

namespace BECore {

    namespace {

        /// Bytes of a client's text that an error message quotes
        constexpr size_t kMaxShownBytes = 64;

    }  // namespace

    Optional<JsonView> FindParam(JsonView params, StringView key) {
        return params.Find(key);
    }

    Optional<String> GetParamString(JsonView params, StringView key) {
        const Optional<JsonView> member = FindParam(params, key);
        if (!member) {
            return std::nullopt;
        }
        String text;
        if (!member->ReadText(text)) {
            return std::nullopt;
        }
        return text;
    }

    Optional<int64_t> GetParamInt(JsonView params, StringView key) {
        const Optional<JsonView> member = FindParam(params, key);
        return member ? member->AsInt64() : std::nullopt;
    }

    Optional<double> GetParamNumber(JsonView params, StringView key) {
        const Optional<JsonView> member = FindParam(params, key);
        if (!member) {
            return std::nullopt;
        }
        const Optional<double> number = member->AsDouble();
        if (!number || !std::isfinite(*number)) {
            return std::nullopt;
        }
        return number;
    }

    Optional<bool> GetParamBool(JsonView params, StringView key) {
        const Optional<JsonView> member = FindParam(params, key);
        return member ? member->AsBool() : std::nullopt;
    }

    std::expected<String, ControlError> RequireParamString(JsonView params, StringView key) {
        Optional<String> text = GetParamString(params, key);
        if (!text.has_value()) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, Format("'{}' has to be a string", key)));
        }
        return std::move(*text);
    }

    String ShownClientText(StringView text) {
        if (text.size() <= kMaxShownBytes) {
            return String(text);
        }
        String shown(text.substr(0, kMaxShownBytes));
        shown += "...";
        return shown;
    }

}  // namespace BECore
