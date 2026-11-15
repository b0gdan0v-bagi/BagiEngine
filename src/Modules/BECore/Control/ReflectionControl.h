#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// The error codes of the by-name methods that are not a `PropertyErrorCode` or a `CallStatus`; they are part of the protocol
    namespace ReflectionControlErrors {
        inline constexpr StringView kUnknownRoot = "unknown_root";
        inline constexpr StringView kNoObject = "no_object";
    }  // namespace ReflectionControlErrors

    /**
     * @brief Registers the methods that reach the objects under the roots of @p service by name (`ControlRoots`)
     *
     * `get_property` and `set_property` here; the other methods of the by-name surface are added next to them. Everything in a request
     * is client data: the names of a root, a path and a method are looked up as text, never interned. Registering twice replaces the
     * handlers.
     */
    void RegisterReflectionControl(ControlService& service);

}  // namespace BECore
