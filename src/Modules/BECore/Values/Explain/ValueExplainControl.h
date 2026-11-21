#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// The error codes of `explain_value` that no other method has; they are part of the protocol
    namespace ValueExplainControlErrors {
        /// The path ends at a field that is not a `Value<T>` of a type `ValueExplainers` knows
        inline constexpr StringView kNotAValue = "not_a_value";
        /// The request names a context that no provider of `ValueContextRegistry` gives
        inline constexpr StringView kUnknownContext = "unknown_context";
        /// The path goes through an element or a pointee, so the field has no fixed place in the object
        inline constexpr StringView kNotFixed = "not_fixed";
    }  // namespace ValueExplainControlErrors

    /**
     * @brief Registers `explain_value {root, path, context?}`: where a `Value<T>` field of a root gets its value, against a named context
     *
     * Answers `{kind, source, has_value, value?, why?, trace?, truncated?}`. Every member of the request is client data: the names are looked up as text, never interned, and no request asserts.
     * Registers the builtin `ValueExplainers` too, so a game adds only the types of its own. Called by `RegisterControlBuiltins`.
     */
    void RegisterValueExplainControl(ControlService& service);

}  // namespace BECore
