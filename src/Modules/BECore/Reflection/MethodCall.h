#pragma once

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// Why a call by name did not run the method; none of them changes the object
    CORE_ENUM(CallStatus, uint8_t, unknown_method, not_callable, const_object, bad_arguments)

    /// A refused call: the status and a message that names the method and, for bad arguments, the argument and the reason
    struct CallError {
        CallStatus status = CallStatus::unknown_method;
        String message;
    };

    namespace MethodCallLimits {

        /// Bytes of a method name a message quotes (the name is client text)
        inline constexpr size_t kMaxShownNameBytes = 64;

    }  // namespace MethodCallLimits

    /**
     * @brief Calls the `BE_FUNCTION` method @p name of @p object, its arguments read from @p args and its result written to @p result
     *
     * The method is found in the object's type and then in its ancestors (the nearest wins). Each parameter is read from the
     * current scope of @p args under its declared name (`arg0`, `arg1`, ... when the declaration names none); the result of a
     * non-void method is written to @p result under the key `result`. For the call the reader is made strict (a wrong type, a
     * number out of range, a missing argument and an unknown key are all errors, not defaults) and the diagnostics are `Error`; both
     * are put back afterwards. The method runs only when the arguments loaded without an error, so a refused call changes nothing:
     * `bad_arguments` quotes the first errors, each under its argument's name, and the errors stay in @p args for the caller. @p args should
     * hold no error before the call, or the call is refused with that error. A non-const method of a const @p object is
     * `const_object`, a method that is listed but not `callable` is `not_callable` (it is never invoked), an unknown name is
     * `unknown_method`. Client text: nothing is interned by the lookup, nothing asserts.
     *
     * Use `InvokeMethod` (`TypeTraits.h`) where the type and the arguments are known at compile time; this is for a name that
     * arrives at run time.
     */
    [[nodiscard]] std::expected<void, CallError> CallMethod(TypedRef object, StringView name, IDeserializer& args, ISerializer& result, const TypeRegistry& registry = TypeRegistry::GetInstance());

}  // namespace BECore
