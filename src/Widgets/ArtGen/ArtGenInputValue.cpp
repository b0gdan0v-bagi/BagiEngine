#include "ArtGenInputValue.h"

#include <BECore/Control/JsonViewOps.h>
#include <Widgets/ArtGen/ArtGenFileText.h>
#include <cstdio>

namespace BECore {

    ArtGenInputValue ArtGenInputValue::FromBool(bool value) {
        ArtGenInputValue result;
        result.kind = ArtGenInputKind::Bool;
        result.flag = value;
        return result;
    }

    ArtGenInputValue ArtGenInputValue::FromInteger(int64_t value) {
        ArtGenInputValue result;
        result.kind = ArtGenInputKind::Integer;
        result.integer = value;
        return result;
    }

    ArtGenInputValue ArtGenInputValue::FromNumber(double value) {
        ArtGenInputValue result;
        result.kind = ArtGenInputKind::Number;
        result.number = value;
        return result;
    }

    ArtGenInputValue ArtGenInputValue::FromText(StringView value) {
        ArtGenInputValue result;
        result.kind = ArtGenInputKind::Text;
        result.text = String(value);
        return result;
    }

    ArtGenInputValue ArtGenInputValue::Read(JsonView value) {
        if (!value.IsValid() || value.IsNull()) {
            return {};
        }
        if (const Optional<bool> flag = value.AsBool()) {
            return FromBool(*flag);
        }
        if (value.IsInteger()) {
            if (const Optional<int64_t> integer = value.AsInt64()) {
                return FromInteger(*integer);
            }
        } else if (value.IsNumber()) {
            if (const Optional<double> number = value.AsDouble()) {
                return FromNumber(*number);
            }
        } else if (value.IsString()) {
            ArtGenInputValue result;
            result.kind = ArtGenInputKind::Text;
            value.ReadText(result.text);
            return result;
        }
        ArtGenInputValue other;
        other.kind = ArtGenInputKind::Other;
        other.text = SortedTextOf(value);
        return other;
    }

    void ArtGenInputValue::Write(JsonWriter& writer) const {
        switch (kind) {
            case ArtGenInputKind::None:
                writer.Null();
                break;
            case ArtGenInputKind::Bool:
                writer.Value(flag);
                break;
            case ArtGenInputKind::Integer:
                writer.Value(integer);
                break;
            case ArtGenInputKind::Number:
                writer.Value(number);
                break;
            case ArtGenInputKind::Text:
                writer.Value(StringView(text));
                break;
            case ArtGenInputKind::Other:
                writer.Raw(StringView(text));
                break;
        }
    }

    void ArtGenInputValue::ToText(char* out, size_t size) const {
        switch (kind) {
            case ArtGenInputKind::Text:
                std::snprintf(out, size, "%.*s", static_cast<int>(text.size()), text.data());
                break;
            case ArtGenInputKind::Integer:
                std::snprintf(out, size, "%lld", static_cast<long long>(integer));
                break;
            case ArtGenInputKind::Number:
                std::snprintf(out, size, "%g", number);
                break;
            case ArtGenInputKind::Bool:
                std::snprintf(out, size, "%s", flag ? "true" : "false");
                break;
            case ArtGenInputKind::None:
            case ArtGenInputKind::Other:
                out[0] = '\0';
                break;
        }
    }

    bool ArtGenInputValue::SameStored(const ArtGenInputValue& other) const {
        if (kind == ArtGenInputKind::Other && other.kind == ArtGenInputKind::Other) {
            return text == other.text;
        }
        return *this == other;
    }

    bool ArtGenInputValue::operator==(const ArtGenInputValue& other) const {
        const bool numeric = kind == ArtGenInputKind::Integer || kind == ArtGenInputKind::Number;
        const bool otherNumeric = other.kind == ArtGenInputKind::Integer || other.kind == ArtGenInputKind::Number;
        if (numeric && otherNumeric) {
            if (kind == other.kind) {
                return kind == ArtGenInputKind::Integer ? integer == other.integer : number == other.number;
            }
            const double asDouble = kind == ArtGenInputKind::Integer ? static_cast<double>(integer) : number;
            const double otherAsDouble = other.kind == ArtGenInputKind::Integer ? static_cast<double>(other.integer) : other.number;
            return asDouble == otherAsDouble;
        }
        if (kind != other.kind) {
            return false;
        }
        switch (kind) {
            case ArtGenInputKind::None:
                return true;
            case ArtGenInputKind::Bool:
                return flag == other.flag;
            case ArtGenInputKind::Text:
                return text == other.text;
            case ArtGenInputKind::Other:
                return text == other.text || SameJsonText(text, other.text);
            case ArtGenInputKind::Integer:
            case ArtGenInputKind::Number:
                break;
        }
        return false;
    }

}  // namespace BECore
