#pragma once

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// Why the program, or its answer, did not give what the session asked for: a peer's words, untrusted. Not set while there is nothing to say
    struct RemoteCallError {
        /// Empty for an answer that was not refused but cannot be used
        String code;
        String message;

        [[nodiscard]] bool IsSet() const {
            return !code.empty() || !message.empty();
        }

        void Clear() {
            code.clear();
            message.clear();
        }

        /// Takes the `error` of a refused response, which is set after it whatever the response holds
        void Read(JsonView error) {
            Clear();
            FindParam(error, "code").value_or(JsonView{}).ReadText(code);
            FindParam(error, "message").value_or(JsonView{}).ReadText(message);
            if (!IsSet()) {
                message = "the program gave no reason";
            }
        }
    };

}  // namespace BECore
