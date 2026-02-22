#include "ConfigFieldSchema.h"

#include <BECore/Format/Format.h>
#include <charconv>

namespace BECore {

    eastl::string ConfigFieldSchema::MakeKey(PoolString configName, eastl::string_view attrPath) {
        return Format("{}|{}", configName.CStr(), attrPath);
    }

    void ConfigFieldSchema::RegisterFlags(PoolString configName, eastl::string_view attrPath,
                                          eastl::vector<eastl::string> flagNames) {
        const eastl::string key = MakeKey(configName, attrPath);
        FieldHint hint;
        hint.type = FieldType::Flags;
        hint.options = std::move(flagNames);
        _hints[key] = std::move(hint);
    }

    FieldHint ConfigFieldSchema::Resolve(PoolString configName, eastl::string_view attrPath,
                                         eastl::string_view currentValue) const {
        const eastl::string key = MakeKey(configName, attrPath);
        if (const auto it = _hints.find(key); it != _hints.end()) {
            return it->second;
        }

        // Heuristic: bool
        if (currentValue == "true" || currentValue == "false") {
            return FieldHint{FieldType::Bool};
        }

        // Heuristic: flags (value contains '|' separator)
        if (currentValue.find('|') != eastl::string_view::npos) {
            FieldHint hint;
            hint.type = FieldType::Flags;
            eastl::string_view remaining = currentValue;
            while (!remaining.empty()) {
                const auto pos = remaining.find('|');
                eastl::string_view token =
                    (pos == eastl::string_view::npos) ? remaining : remaining.substr(0, pos);
                while (!token.empty() && token.front() == ' ') {
                    token.remove_prefix(1);
                }
                while (!token.empty() && token.back() == ' ') {
                    token.remove_suffix(1);
                }
                if (!token.empty()) {
                    hint.options.emplace_back(token.data(), token.size());
                }
                if (pos == eastl::string_view::npos) {
                    break;
                }
                remaining = remaining.substr(pos + 1);
            }
            return hint;
        }

        // Heuristic: integer
        {
            int result = 0;
            const auto [ptr, ec] =
                std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return FieldHint{FieldType::Int};
            }
        }

        // Heuristic: float
        {
            float result = 0.0f;
            const auto [ptr, ec] =
                std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return FieldHint{FieldType::Float};
            }
        }

        return FieldHint{FieldType::String};
    }

} // namespace BECore
