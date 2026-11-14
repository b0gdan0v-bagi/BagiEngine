#include "ConfigFieldSchema.h"

namespace BECore {

    String ConfigFieldSchema::MakeKey(PoolString configName, StringView attrPath) {
        return Format("{}|{}", configName.CStr(), attrPath);
    }

    void ConfigFieldSchema::RegisterFlags(PoolString configName, StringView attrPath, Vector<String> flagNames) {
        const String key = MakeKey(configName, attrPath);
        FieldHint hint;
        hint.type = FieldType::Flags;
        hint.options = std::move(flagNames);
        _hints[key] = std::move(hint);
    }

    FieldHint ConfigFieldSchema::Resolve(PoolString configName, StringView attrPath, StringView currentValue) const {
        const String key = MakeKey(configName, attrPath);
        if (const auto it = _hints.find(key); it != _hints.end()) {
            return it->second;
        }

        // Heuristic: bool
        if (currentValue == "true" || currentValue == "false") {
            return FieldHint{FieldType::Bool};
        }

        // Heuristic: flags (value contains '|' separator)
        if (currentValue.find('|') != StringView::npos) {
            FieldHint hint;
            hint.type = FieldType::Flags;
            StringView remaining = currentValue;
            while (!remaining.empty()) {
                const auto pos = remaining.find('|');
                StringView token = (pos == StringView::npos) ? remaining : remaining.substr(0, pos);
                while (!token.empty() && token.front() == ' ') {
                    token.remove_prefix(1);
                }
                while (!token.empty() && token.back() == ' ') {
                    token.remove_suffix(1);
                }
                if (!token.empty()) {
                    hint.options.emplace_back(token.data(), token.size());
                }
                if (pos == StringView::npos) {
                    break;
                }
                remaining = remaining.substr(pos + 1);
            }
            return hint;
        }

        // Heuristic: integer
        {
            int result = 0;
            const auto [ptr, ec] = std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return FieldHint{FieldType::Int};
            }
        }

        // Heuristic: float
        {
            float result = 0.0f;
            const auto [ptr, ec] = std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return FieldHint{FieldType::Float};
            }
        }

        return FieldHint{FieldType::String};
    }

}  // namespace BECore
