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

    String DumpJson(const Json& value) {
        //-NoStd: nlohmann's dump
        const std::string text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
        return String(text.c_str(), text.size());
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

    Optional<String> GetJsonString(const Json& object, StringView key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_string()) {
            return std::nullopt;
        }
        //-NoStd: nlohmann's string
        const std::string& text = member->get_ref<const std::string&>();
        return String(text.c_str(), text.size());
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

}  // namespace BECore
