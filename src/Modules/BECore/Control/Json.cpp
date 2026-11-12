#include "Json.h"

namespace BECore {

    bool IsJsonNestedDeeperThan(eastl::string_view text, size_t maxDepth) {
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

    std::expected<Json, ControlError> ParseJson(eastl::string_view text) {
        // allow_exceptions = false: bad input gives a discarded value instead of a throw (an abort in this build)
        Json parsed = Json::parse(text.data(), text.data() + text.size(), nullptr, false);
        if (parsed.is_discarded()) {
            return std::unexpected(MakeControlError(ControlErrors::kParseError, "the text is not valid JSON"));
        }
        return parsed;
    }

    eastl::string DumpJson(const Json& value) {
        const std::string text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
        return eastl::string(text.c_str(), text.size());
    }

    Json MakeJsonString(eastl::string_view text) {
        return Json(std::string(text.data(), text.size()));
    }

    const Json* FindJsonMember(const Json& object, eastl::string_view key) {
        if (!object.is_object()) {
            return nullptr;
        }
        const auto it = object.find(std::string_view(key.data(), key.size()));
        return it == object.end() ? nullptr : &*it;
    }

    std::optional<eastl::string> GetJsonString(const Json& object, eastl::string_view key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_string()) {
            return std::nullopt;
        }
        const std::string& text = member->get_ref<const std::string&>();
        return eastl::string(text.c_str(), text.size());
    }

    std::optional<int64_t> GetJsonInt(const Json& object, eastl::string_view key) {
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

    std::optional<double> GetJsonNumber(const Json& object, eastl::string_view key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_number()) {
            return std::nullopt;
        }
        return member->get<double>();
    }

    std::optional<bool> GetJsonBool(const Json& object, eastl::string_view key) {
        const Json* member = FindJsonMember(object, key);
        if (member == nullptr || !member->is_boolean()) {
            return std::nullopt;
        }
        return member->get<bool>();
    }

}  // namespace BECore
