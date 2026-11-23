#include "ConfigFieldSchema.h"

#include <BECore/Memory/ArenaAllocator.h>
#include <EASTL/iterator.h>

namespace BECore {

    namespace {

        // Room for "<config>|<element>/@<attribute>" so the key is built in one block
        constexpr size_t kKeyReserve = 128;

        const FieldHint& DefaultHint(FieldType type) {
            static const FieldHint stringHint{FieldType::String};
            static const FieldHint boolHint{FieldType::Bool};
            static const FieldHint intHint{FieldType::Int};
            static const FieldHint floatHint{FieldType::Float};
            switch (type) {
                case FieldType::Bool:
                    return boolHint;
                case FieldType::Int:
                    return intHint;
                case FieldType::Float:
                    return floatHint;
                default:
                    return stringHint;
            }
        }

    }  // namespace

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

    const FieldHint& ConfigFieldSchema::Resolve(PoolString configName, StringView attrPath, StringView currentValue) const {
        const ScratchScope scratch;
        ArenaString key;
        key.reserve(kKeyReserve);
        FormatTo(eastl::back_inserter(key), "{}|{}", configName.CStr(), attrPath);
        if (const auto it = _hints.find(StringView(key.data(), key.size())); it != _hints.end()) {
            return it->second;
        }

        if (currentValue == "true" || currentValue == "false") {
            return DefaultHint(FieldType::Bool);
        }

        {
            int result = 0;
            const auto [ptr, ec] = std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return DefaultHint(FieldType::Int);
            }
        }

        {
            float result = 0.0f;
            const auto [ptr, ec] = std::from_chars(currentValue.data(), currentValue.data() + currentValue.size(), result);
            if (ec == std::errc{} && ptr == currentValue.data() + currentValue.size()) {
                return DefaultHint(FieldType::Float);
            }
        }

        return DefaultHint(FieldType::String);
    }

}  // namespace BECore
