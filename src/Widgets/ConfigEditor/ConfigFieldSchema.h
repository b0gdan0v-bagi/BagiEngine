#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/Singleton.h>

namespace BECore {

    /**
     * @brief Registrations that do nothing, so that a game which registers its config fields still builds
     *
     * The Config Editor takes the fields of a config and their controls from the type registered for it with `CookableConfig<T>`; a
     * config with no such type is edited as text.
     */
    class ConfigFieldSchema : public Singleton<ConfigFieldSchema> {
    public:
        template <typename EnumT>
        [[deprecated("register the config's type with CookableConfig")]] void RegisterEnum(PoolString /*configName*/, StringView /*attrPath*/) {}

        template <typename BaseT>
        [[deprecated("register the config's type with CookableConfig")]] void RegisterFactoryTypes(PoolString /*configName*/, StringView /*attrPath*/) {}

        [[deprecated("register the config's type with CookableConfig")]] void RegisterFlags(PoolString /*configName*/, StringView /*attrPath*/, const Vector<String>& /*flagNames*/) {}
    };

}  // namespace BECore
