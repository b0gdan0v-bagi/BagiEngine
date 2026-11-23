#include "ControlParams.h"

#include <cmath>

namespace BECore {

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

}  // namespace BECore
