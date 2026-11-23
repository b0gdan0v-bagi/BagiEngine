#pragma once

#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Checked readers of the `params` of a control call, the ones `GetJson*` are over a `Json`
     *
     * @p params is the view a handler got from `ControlCall`: an object. Any other value, an absent key or a value of the wrong type reads as
     * nothing and never asserts, because the text is a client's. A handler that has to tell "absent" from "wrong type" asks FindParam first
     * and reads the value it found.
     */

    /// The member @p key of @p params; nullopt when @p params is not an object or has no such member
    [[nodiscard]] Optional<JsonView> FindParam(JsonView params, StringView key);

    /// A string member, decoded (an escape is resolved, an embedded NUL stays); nullopt when absent or not a string
    [[nodiscard]] Optional<String> GetParamString(JsonView params, StringView key);

    /// An integer member that fits `int64_t`; nullopt when absent, not an integer (`1.5`, `"3"` and a bool are not) or past 64 bits
    [[nodiscard]] Optional<int64_t> GetParamInt(JsonView params, StringView key);

    /// An integer or a floating-point member that is finite (`1e999` is nothing, as the DOM refused it at parse); nullopt when absent or not a number
    [[nodiscard]] Optional<double> GetParamNumber(JsonView params, StringView key);

    /// A boolean member; nullopt when absent or not a boolean
    [[nodiscard]] Optional<bool> GetParamBool(JsonView params, StringView key);

}  // namespace BECore
