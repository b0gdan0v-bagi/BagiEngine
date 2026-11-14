#include "UITidyMessage.h"

namespace BECore::UIText {

    namespace {

        constexpr StringView CutMark = "...";
        constexpr uint8_t ContinuationMask = 0xC0;
        constexpr uint8_t ContinuationBits = 0x80;
        constexpr uint8_t FirstPrintable = 0x20;
        constexpr uint8_t DeleteCharacter = 0x7F;

        String Tidy(StringView text, size_t maxBytes) {
            size_t length = text.size();
            if (length > maxBytes) {
                length = maxBytes - CutMark.size();
                // text[length] is the first byte left out: when it continues a character, that character goes too
                while (length > 0 && (static_cast<uint8_t>(text[length]) & ContinuationMask) == ContinuationBits) {
                    --length;
                }
            }
            String result(text.data(), length);
            for (char& c : result) {
                const auto byte = static_cast<uint8_t>(c);
                if (byte < FirstPrintable || byte == DeleteCharacter) {
                    c = ' ';
                }
            }
            if (length < text.size()) {
                result.append(CutMark.data(), CutMark.size());
            }
            return result;
        }

    }  // namespace

    String TidyMessage(StringView text) {
        return Tidy(text, kMaxMessageBytes);
    }

    String Excerpt(StringView text) {
        return Tidy(text, kMaxExcerptBytes);
    }

}  // namespace BECore::UIText
