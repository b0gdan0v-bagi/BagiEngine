#pragma once

#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>
#include <EASTL/algorithm.h>

namespace BECore {

    /// Why the program, or its answer, did not give what the session asked for: a peer's words, untrusted. Not set while there is nothing to say
    struct RemoteCallError {
        /// A panel shows each text as a wrapped line: a longer one would push what is drawn under it out of the window
        static constexpr size_t kMaxTextBytes = 256;

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
            ReadLine(error, "code", code);
            ReadLine(error, "message", message);
            if (!IsSet()) {
                message = "the program gave no reason";
            }
        }

    private:
        /// The member @p key as one line of at most kMaxTextBytes and the mark of the cut
        static void ReadLine(JsonView error, StringView key, String& out) {
            FindParam(error, key).value_or(JsonView{}).ReadText(out);
            if (out.size() > kMaxTextBytes) {
                size_t end = kMaxTextBytes;
                // Never inside a UTF-8 sequence
                while (end > 0 && (static_cast<uint8_t>(out[end]) & 0xC0u) == 0x80u) {
                    --end;
                }
                out.resize(end);
                out += "...";
            }
            eastl::replace(out.begin(), out.end(), '\n', ' ');
        }
    };

}  // namespace BECore
