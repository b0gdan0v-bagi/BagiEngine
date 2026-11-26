#include "GameCommandForm.h"

#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Control/JsonWriter.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kJsonSpaces = " \t\r\n";

        bool IsBlank(StringView text) {
            return text.find_first_not_of(kJsonSpaces) == StringView::npos;
        }

        /// Writes the value of @p field into @p writer from the text of a field that is not empty, parsed with @p tape; the line that says why it cannot
        std::expected<void, String> WriteTyped(const GameCommandFormField& field, StringView text, JsonTape& tape, JsonWriter& writer) {
            if (field.param.type == GameCommandParamType::String) {
                writer.Value(text);
                return {};
            }
            const auto parsed = tape.Parse(text);
            const JsonView value(tape, 0);
            if (field.param.type == GameCommandParamType::Json) {
                if (!parsed) {
                    return std::unexpected(Format("'{}' is not a JSON value: {}", field.param.name, parsed.error().message));
                }
            } else if (field.param.type == GameCommandParamType::Integer) {
                if (!parsed || !value.AsInt64()) {
                    return std::unexpected(Format("'{}' has to be an integer", field.param.name));
                }
            } else {
                const Optional<double> number = parsed ? value.AsDouble() : std::nullopt;
                if (!number || !std::isfinite(*number)) {
                    return std::unexpected(Format("'{}' has to be a finite number", field.param.name));
                }
            }
            // As typed, so no digit of a number is lost on the way
            writer.Raw(text);
            return {};
        }

    }  // namespace

    GameCommandForm::GameCommandForm(Span<const GameCommandParam> params) {
        _fields.resize(params.size());
        for (size_t i = 0; i < params.size(); ++i) {
            _fields[i].param = params[i];
        }
    }

    std::expected<String, String> GameCommandForm::BuildArguments() const {
        String arguments;
        JsonWriter writer(arguments);
        JsonTape tape;
        writer.BeginObject();
        for (const GameCommandFormField& field : _fields) {
            if (field.param.type == GameCommandParamType::Boolean) {
                writer.Member(field.param.name, field.flag);
                continue;
            }
            const StringView text(field.text);
            // Spaces are a value of a string, and nothing for the rest
            if (field.param.type == GameCommandParamType::String ? text.empty() : IsBlank(text)) {
                if (field.param.required) {
                    return std::unexpected(Format("'{}' is required", field.param.name));
                }
                continue;
            }
            writer.Key(field.param.name);
            if (auto written = WriteTyped(field, text, tape, writer); !written) {
                return std::unexpected(std::move(written.error()));
            }
        }
        writer.EndObject();
        return arguments;
    }

}  // namespace BECore
