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

        if (currentValue == "true" || currentValue == "false") {
            return FieldHint{FieldType::Bool};
        }

        {
            int result = 0;
            const auto [ptr, ec] = std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return FieldHint{FieldType::Int};
            }
        }

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
