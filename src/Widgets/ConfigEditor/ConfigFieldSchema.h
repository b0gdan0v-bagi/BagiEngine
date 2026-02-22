#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/Singleton.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

namespace BECore {

    CORE_ENUM(FieldType, uint8_t, String, Bool, Int, Float, Enum, Flags)

    struct FieldHint {
        FieldType type = FieldType::String;
        eastl::vector<eastl::string> options; // enum values or flag names (owned)
    };

    /**
     * @brief Singleton registry mapping (configName, attrPath) to a FieldHint
     *
     * Supports explicit registration for enum and flags fields.
     * Falls back to heuristic resolution for unregistered fields.
     *
     * Registration paths use the format "elementName/@attributeName" (e.g. "sink/@type").
     */
    class ConfigFieldSchema : public Singleton<ConfigFieldSchema> {
    public:
        /**
         * @brief Register an attribute as an enum field
         * @tparam EnumT Enum type registered via CORE_ENUM
         * @param configName Config file name (without extension)
         * @param attrPath Path in format "elementName/@attrName"
         */
        template <typename EnumT>
        void RegisterEnum(PoolString configName, eastl::string_view attrPath) {
            const eastl::string key = MakeKey(configName, attrPath);
            FieldHint hint;
            hint.type = FieldType::Enum;
            const auto& names = EnumUtils<EnumT>::Names();
            hint.options.reserve(names.size());
            for (const auto& name : names) {
                hint.options.emplace_back(name.data(), name.size());
            }
            _hints[key] = std::move(hint);
        }

        /**
         * @brief Register an attribute as a flags field (multi-checkbox)
         * @param configName Config file name (without extension)
         * @param attrPath Path in format "elementName/@attrName"
         * @param flagNames List of all possible flag names
         */
        void RegisterFlags(PoolString configName, eastl::string_view attrPath,
                           eastl::vector<eastl::string> flagNames);

        /**
         * @brief Resolve a FieldHint for the given attribute
         *
         * Checks the registry first, then falls back to heuristics:
         * - "true" / "false" -> Bool
         * - Contains '|'     -> Flags (options extracted from value)
         * - Parseable as int -> Int
         * - Parseable as float -> Float
         * - Default          -> String
         *
         * @param configName Config file name (without extension)
         * @param attrPath Path in format "elementName/@attrName"
         * @param currentValue Current attribute value for heuristic fallback
         */
        FieldHint Resolve(PoolString configName, eastl::string_view attrPath,
                          eastl::string_view currentValue) const;

    private:
        static eastl::string MakeKey(PoolString configName, eastl::string_view attrPath);

        eastl::unordered_map<eastl::string, FieldHint> _hints;
    };

} // namespace BECore
