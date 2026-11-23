#include "Json.h"

namespace BECore {

    bool IsJsonNestedDeeperThan(StringView text, size_t maxDepth) {
        size_t depth = 0;
        bool inString = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (inString) {
                if (c == '\\') {
                    ++i;
                } else if (c == '"') {
                    inString = false;
                }
            } else if (c == '"') {
                inString = true;
            } else if (c == '[' || c == '{') {
                if (++depth > maxDepth) {
                    return true;
                }
            } else if ((c == ']' || c == '}') && depth > 0) {
                --depth;
            }
        }
        return false;
    }

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

    Optional<int64_t> GetJsonInt(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_number_integer()) {
            return std::nullopt;
        }
        if (member->is_number_unsigned()) {
            const uint64_t value = member->get<uint64_t>();
            if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return std::nullopt;
            }
            return static_cast<int64_t>(value);
        }
        return member->get<int64_t>();
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

    namespace {

        constexpr char kPointerSeparator = '/';
        constexpr char kPointerEscape = '~';

        ControlError BadPointer(StringView why) {
            return MakeControlError(ControlErrors::kInvalidParams, Format("bad JSON pointer: {}", why));
        }

        /// Checks every `~` of @p pointer, which is not empty and starts with `/`
        Optional<ControlError> CheckPointerEscapes(StringView pointer) {
            for (size_t i = 0; i < pointer.size(); ++i) {
                if (pointer[i] != kPointerEscape) {
                    continue;
                }
                const bool valid = i + 1 < pointer.size() && (pointer[i + 1] == '0' || pointer[i + 1] == '1');
                if (!valid) {
                    return BadPointer("`~` must be followed by 0 or 1");
                }
                ++i;
            }
            return std::nullopt;
        }

        /// The key a token names: `~1` is `/`, `~0` is `~`. @p storage holds the text when there is something to unescape
        StringView UnescapeToken(StringView token, String& storage) {
            if (token.find(kPointerEscape) == StringView::npos) {
                return token;
            }
            storage.clear();
            for (size_t i = 0; i < token.size(); ++i) {
                if (token[i] == kPointerEscape) {
                    ++i;
                    storage.push_back(token[i] == '0' ? kPointerEscape : kPointerSeparator);
                } else {
                    storage.push_back(token[i]);
                }
            }
            return StringView(storage.c_str(), storage.size());
        }

        /// The index a token names in an array of @p size elements; nullopt when it is past the end. An error when the token is not an array index
        std::expected<Optional<size_t>, ControlError> ParseArrayIndex(StringView token, size_t size) {
            if (token == "-") {
                return std::unexpected(BadPointer("`-` names the element after the last one, which cannot be read"));
            }
            if (token.empty() || (token.size() > 1 && token[0] == '0')) {
                return std::unexpected(BadPointer("an array index is 0 or digits without a leading zero"));
            }
            size_t index = 0;
            bool pastEnd = false;
            for (const char c : token) {
                if (c < '0' || c > '9') {
                    return std::unexpected(BadPointer("an array index is 0 or digits without a leading zero"));
                }
                // Digits keep being checked after the index left the array, so a bad token is an error whatever its length
                if (!pastEnd) {
                    index = index * 10 + static_cast<size_t>(c - '0');
                    pastEnd = index >= size;
                }
            }
            return pastEnd ? Optional<size_t>() : Optional<size_t>(index);
        }

    }  // namespace

    std::expected<const Json*, ControlError> FindJsonPointer(const Json& document, StringView pointer) {
        if (pointer.empty()) {
            return &document;
        }
        if (pointer[0] != kPointerSeparator) {
            return std::unexpected(BadPointer("it must be empty or start with `/`"));
        }
        if (const auto bad = CheckPointerEscapes(pointer)) {
            return std::unexpected(*bad);
        }

        const Json* current = &document;
        String unescaped;
        size_t start = 1;
        bool absent = false;  // the path left the document; the rest is still checked
        while (start <= pointer.size()) {
            size_t end = pointer.find(kPointerSeparator, start);
            if (end == StringView::npos) {
                end = pointer.size();
            }
            const StringView token = pointer.substr(start, end - start);
            start = end + 1;

            if (absent || current->is_primitive()) {
                absent = true;
            } else if (current->is_object()) {
                current = FindJsonMember(*current, UnescapeToken(token, unescaped));
                absent = current == nullptr;
            } else {
                const auto index = ParseArrayIndex(token, current->size());
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
