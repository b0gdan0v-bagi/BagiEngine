#include <BECore/Reflection/StrictText.h>

namespace BECore::StrictText {

    namespace {

        constexpr char kPlus = '+';
        constexpr char kMinus = '-';

        bool IsAsciiSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

    }  // namespace

    StringView TrimAsciiSpaces(StringView text) {
        while (!text.empty() && IsAsciiSpace(text.front())) {
            text.remove_prefix(1);
        }
        while (!text.empty() && IsAsciiSpace(text.back())) {
            text.remove_suffix(1);
        }
        return text;
    }

    StringView DropPlusSign(StringView text) {
        if (!text.empty() && text.front() == kPlus) {
            text.remove_prefix(1);
            if (text.empty() || text.front() == kPlus || text.front() == kMinus) {
                return {};
            }
        }
        return text;
    }

    Status ParseBool(StringView text, bool& outValue) {
        text = TrimAsciiSpaces(text);
        if (text == "true" || text == "1") {
            outValue = true;
            return Status::Ok;
        }
        if (text == "false" || text == "0") {
            outValue = false;
            return Status::Ok;
        }
        return Status::Malformed;
    }

    String InvalidValueMessage(const char* kind, StringView shown, bool wasCut, Status status) {
        const char* const reason = status == Status::OutOfRange ? " (out of range)" : "";
        const char* const cut = wasCut ? "..." : "";
        return Format("Invalid {} value '{}{}'{}", kind, shown, cut, reason);
    }

}  // namespace BECore::StrictText
