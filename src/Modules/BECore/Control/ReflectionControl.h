#pragma once

#include <BECore/Reflection/TypeInfo.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// The error codes of the by-name methods that are not a `PropertyErrorCode` or a `CallStatus`; they are part of the protocol
    namespace ReflectionControlErrors {
        inline constexpr StringView kUnknownRoot = "unknown_root";
        inline constexpr StringView kNoObject = "no_object";
        inline constexpr StringView kInterning = "interning";
        inline constexpr StringView kUnknownType = "unknown_type";
        /// The wire name of `CallStatus::unknown_method`, renamed so a client does not confuse it with the control method `unknown_method`
        inline constexpr StringView kUnknownFunction = "unknown_function";
    }  // namespace ReflectionControlErrors

    /**
     * @brief Whether a call of @p method puts text into the `PoolString` pool: a parameter holds a `PoolString` anywhere
     *
     * Looks at the parameter, its element, key and alternatives, and the fields of an object type and its ancestors (found through
     * @p registry), once per type, so a recursive type ends. The result is not looked at: reading interns nothing.
     */
    [[nodiscard]] bool MethodInterns(const MethodDesc& method, const TypeRegistry& registry = TypeRegistry::GetInstance());

    /**
     * @brief Registers the methods that reach the objects under the roots of @p service by name (`ControlRoots`)
     *
     * `get_property`, `set_property`, `call_method`, `list_types`, `describe_type` and `schema` here. Everything in a request is client data: the names of a root, a path and a method
     * are looked up as text, never interned. Registering twice replaces the handlers.
     */
    void RegisterReflectionControl(ControlService& service);

}  // namespace BECore
