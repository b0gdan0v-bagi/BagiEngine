#pragma once

#include <BECore/Reflection/AbstractFactory.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

namespace BECore {

    CORE_ENUM(FieldType, uint8_t, String, Bool, Int, Float, Enum, Flags)

    struct FieldHint {
        FieldType type = FieldType::String;
        Vector<String> options;  // enum values or flag names (owned)
    };

    /**
     * @brief Singleton registry mapping (configName, attrPath) to a FieldHint
     *
     * Supports explicit registration for enum, factory type and flags fields.
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
        void RegisterEnum(PoolString configName, StringView attrPath) {
            const String key = MakeKey(configName, attrPath);
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
         * @brief Register an attribute as an enum field whose values are the class names of an AbstractFactory
         *
         * The options are the type names of every type registered in AbstractFactory<BaseT>, sorted
         * alphabetically (the registration order depends on static initializers). They are exactly the strings
         * AbstractFactory<BaseT>::Create(eastl::string_view) resolves, which is how the loader reads the
         * "type" attribute of a factory element.
         *
         * @tparam BaseT Factory base type (e.g. ILogSink)
         * @param configName Config file name (without extension)
         * @param attrPath Path in format "elementName/@attrName"
         */
        template <typename BaseT>
        void RegisterFactoryTypes(PoolString configName, StringView attrPath) {
            const String key = MakeKey(configName, attrPath);
            FieldHint hint;
            hint.type = FieldType::Enum;
            const auto& metas = AbstractFactory<BaseT>::GetInstance().GetRegisteredTypes();
            hint.options.reserve(metas.size());
            for (const auto& meta : metas) {
                hint.options.emplace_back(meta.typeName.data(), meta.typeName.size());
            }
            eastl::sort(hint.options.begin(), hint.options.end());
            _hints[key] = std::move(hint);
        }

        /**
         * @brief Register an attribute as a flags field (multi-checkbox)
         * @param configName Config file name (without extension)
         * @param attrPath Path in format "elementName/@attrName"
         * @param flagNames List of all possible flag names
         */
        void RegisterFlags(PoolString configName, StringView attrPath, Vector<String> flagNames);

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
        FieldHint Resolve(PoolString configName, StringView attrPath, StringView currentValue) const;

    private:
        static String MakeKey(PoolString configName, StringView attrPath);

        HashMap<String, FieldHint> _hints;
    };

}  // namespace BECore
