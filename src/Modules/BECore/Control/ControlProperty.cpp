#include "ControlProperty.h"

#include <BECore/Control/ControlParams.h>
#include <cmath>

namespace BECore {

    namespace {

        std::unexpected<ControlError> InvalidValue(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

    }  // namespace

    std::expected<PropertyValue, ControlError> ReadPropertyValue(JsonView params) {
        const Optional<JsonView> value = FindParam(params, "value");
        if (!value) {
            return InvalidValue("'value' is missing");
        }
        switch (value->Kind()) {
            case JsonKind::Bool:
                return PropertyValue(std::in_place_type<bool>, *value->AsBool());
            case JsonKind::Integer: {
                if (const Optional<int64_t> number = value->AsInt64()) {
                    return PropertyValue(std::in_place_type<int64_t>, *number);
                }
                if (const Optional<uint64_t> number = value->AsUint64()) {
                    return PropertyValue(std::in_place_type<uint64_t>, *number);
                }
                [[fallthrough]];
            }
            case JsonKind::Number: {
                const Optional<double> number = value->AsDouble();
                if (!number || !std::isfinite(*number)) {
                    return InvalidValue("'value' has to be a finite number");
                }
                return PropertyValue(std::in_place_type<double>, *number);
            }
            case JsonKind::String: {
                String text;
                value->ReadText(text);
                return PropertyValue(std::in_place_type<String>, std::move(text));
            }
            case JsonKind::Null:
            case JsonKind::Array:
            case JsonKind::Object:
                break;
        }
        return InvalidValue("'value' has to be a boolean, a number or a string");
    }

    void WritePropertyValue(JsonWriter& writer, const PropertyValue& value) {
        std::visit(
            [&writer](const auto& held) {
                using Held = std::decay_t<decltype(held)>;
                if constexpr (std::is_same_v<Held, String>) {
                    writer.Value(StringView(held));
                } else {
                    writer.Value(held);
                }
            },
            value);
    }

    ControlError MakePropertyControlError(const PropertyError& error) {
        const PropertyErrorCode code = error.code == PropertyErrorCode::too_long ? PropertyErrorCode::invalid_path : error.code;
        return MakeControlError(EnumUtils<PropertyErrorCode>::ToString(code), error.message);
    }

}  // namespace BECore
