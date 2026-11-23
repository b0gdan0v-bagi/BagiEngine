#include "Json.h"

#include <BECore/Control/JsonPointerSyntax.h>

namespace BECore {

    std::expected<Json, ControlError> ParseJson(StringView text) {
        // allow_exceptions = false: bad input gives a discarded value instead of a throw (an abort in this build)
        Json parsed = Json::parse(text.data(), text.data() + text.size(), nullptr, false);
        if (parsed.is_discarded()) {
            return std::unexpected(MakeControlError(ControlErrors::kParseError, "the text is not valid JSON"));
        }
        return parsed;
    }

    namespace {

        void WriteJsonAt(JsonWriter& writer, const Json& value) {
            switch (value.type()) {
                case Json::value_t::object:
                    if (writer.GetDepth() >= JsonWriter::kMaxDepth) {
                        writer.Null();
                        return;
                    }
                    writer.BeginObject();
                    for (auto it = value.cbegin(); it != value.cend(); ++it) {
                        const auto& key = it.key();
                        writer.Key(StringView(key.data(), key.size()));
                        WriteJsonAt(writer, *it);
                    }
                    writer.EndObject();
                    return;
                case Json::value_t::array:
                    if (writer.GetDepth() >= JsonWriter::kMaxDepth) {
                        writer.Null();
                        return;
                    }
                    writer.BeginArray();
                    for (const Json& element : value) {
                        WriteJsonAt(writer, element);
                    }
                    writer.EndArray();
                    return;
                case Json::value_t::string: {
                    const auto& text = value.get_ref<const Json::string_t&>();
                    writer.Value(StringView(text.data(), text.size()));
                    return;
                }
                case Json::value_t::boolean:
                    writer.Value(value.get<bool>());
                    return;
                case Json::value_t::number_integer:
                    writer.Value(value.get<int64_t>());
                    return;
                case Json::value_t::number_unsigned:
                    writer.Value(value.get<uint64_t>());
                    return;
                case Json::value_t::number_float:
                    writer.Value(value.get<double>());
                    return;
                default:
                    // null, and the binary and discarded values that no parsed or built control document holds
                    writer.Null();
                    return;
            }
        }

    }  // namespace

    void WriteJson(JsonWriter& writer, const Json& value) {
        WriteJsonAt(writer, value);
    }

    void AppendJson(String& out, const Json& value, int indent) {
        JsonWriter writer(out, indent);
        WriteJson(writer, value);
    }

    String DumpJson(const Json& value, int indent) {
        String out;
        AppendJson(out, value, indent);
        return out;
    }

    Json MakeJsonString(StringView text) {
        return Json(ToStdString(text));
    }

    const Json* FindJsonMember(const Json& object, StringView key) {
        if (!object.is_object()) {
            return nullptr;
        }
        const auto it = object.find(ToStd(key));
        return it == object.end() ? nullptr : &*it;
    }

    Json* FindJsonMember(Json& object, StringView key) {
        if (!object.is_object()) {
            return nullptr;
        }
        const auto it = object.find(ToStd(key));
        return it == object.end() ? nullptr : &*it;
    }

    Optional<StringView> JsonStringView(const Json& value) {
        if (!value.is_string()) {
            return std::nullopt;
        }
        //-NoStd: nlohmann's string
        return FromStd(value.get_ref<const std::string&>());
    }

    Optional<StringView> JsonStringView(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr) {
            return std::nullopt;
        }
        return JsonStringView(*member);
    }

    Optional<String> GetJsonString(const Json& object, StringView key) {
        const Optional<StringView> text = JsonStringView(object, key);
        if (!text) {
            return std::nullopt;
        }
        return String(text->data(), text->size());
    }

    Optional<int64_t> GetJsonIntValue(const Json& value) {
        if (!value.is_number_integer()) {
            return std::nullopt;
        }
        if (value.is_number_unsigned()) {
            const uint64_t number = value.get<uint64_t>();
            if (number > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return std::nullopt;
            }
            return static_cast<int64_t>(number);
        }
        return value.get<int64_t>();
    }

    Optional<int64_t> GetJsonInt(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr) {
            return std::nullopt;
        }
        return GetJsonIntValue(*member);
    }

    Optional<double> GetJsonNumber(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_number()) {
            return std::nullopt;
        }
        return member->get<double>();
    }

    Optional<bool> GetJsonBool(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_boolean()) {
            return std::nullopt;
        }
        return member->get<bool>();
    }

    std::expected<const Json*, ControlError> FindJsonPointer(const Json& document, StringView pointer) {
        if (pointer.empty()) {
            return &document;
        }
        if (pointer[0] != Detail::kPointerSeparator) {
            return std::unexpected(Detail::BadPointer("it must be empty or start with `/`"));
        }
        if (const auto bad = Detail::CheckPointerEscapes(pointer)) {
            return std::unexpected(*bad);
        }

        const Json* current = &document;
        String unescaped;
        size_t start = 1;
        bool absent = false;  // the path left the document; the rest is still checked
        while (start <= pointer.size()) {
            size_t end = pointer.find(Detail::kPointerSeparator, start);
            if (end == StringView::npos) {
                end = pointer.size();
            }
            const StringView token = pointer.substr(start, end - start);
            start = end + 1;

            if (absent || current->is_primitive()) {
                absent = true;
            } else if (current->is_object()) {
                current = FindJsonMember(*current, Detail::UnescapePointerToken(token, unescaped));
                absent = current == nullptr;
            } else {
                const auto index = Detail::ParseArrayIndex(token, current->size());
                if (!index) {
                    return std::unexpected(index.error());
                }
                absent = !index->has_value();
                current = absent ? nullptr : &(*current)[**index];
            }
        }
        return absent ? nullptr : current;
    }

}  // namespace BECore
