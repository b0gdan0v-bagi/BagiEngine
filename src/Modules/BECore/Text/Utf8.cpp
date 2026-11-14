#include <BECore/Text/Utf8.h>

namespace BECore::Utf8 {

    namespace {

        constexpr uint8_t kContinuationMask = 0xC0;
        constexpr uint8_t kContinuationTag = 0x80;
        constexpr uint8_t kContinuationPayload = 0x3F;
        constexpr int kContinuationBits = 6;
        constexpr char32_t kMaxCodePoint = 0x10FFFF;
        constexpr char32_t kSurrogateFirst = 0xD800;
        constexpr char32_t kSurrogateLast = 0xDFFF;

        bool IsContinuation(uint8_t byte) {
            return (byte & kContinuationMask) == kContinuationTag;
        }

        bool IsScalarValue(char32_t codePoint) {
            return codePoint <= kMaxCodePoint && !(codePoint >= kSurrogateFirst && codePoint <= kSurrogateLast);
        }

    }  // namespace

    char32_t DecodeNext(StringView text, size_t& pos) {
        if (pos >= text.size()) {
            return 0;
        }
        const auto lead = static_cast<uint8_t>(text[pos]);
        size_t length = 0;
        char32_t codePoint = 0;
        char32_t smallest = 0;  // below this the sequence is overlong
        if (lead < 0x80) {
            ++pos;
            return lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2;
            codePoint = lead & 0x1F;
            smallest = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
            codePoint = lead & 0x0F;
            smallest = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4;
            codePoint = lead & 0x07;
            smallest = 0x10000;
        } else {
            ++pos;  // a stray continuation byte or an impossible lead byte
            return kReplacement;
        }

        if (pos + length > text.size()) {
            ++pos;
            return kReplacement;
        }
        for (size_t i = 1; i < length; ++i) {
            const auto next = static_cast<uint8_t>(text[pos + i]);
            if (!IsContinuation(next)) {
                ++pos;
                return kReplacement;
            }
            codePoint = (codePoint << kContinuationBits) | (next & kContinuationPayload);
        }
        if (codePoint < smallest || !IsScalarValue(codePoint)) {
            ++pos;
            return kReplacement;
        }
        pos += length;
        return codePoint;
    }

    void Append(eastl::string& out, char32_t codePoint) {
        if (!IsScalarValue(codePoint)) {
            codePoint = kReplacement;
        }
        const auto push = [&out](uint32_t value) { out.push_back(static_cast<char>(value)); };
        if (codePoint < 0x80) {
            push(codePoint);
        } else if (codePoint < 0x800) {
            push(0xC0 | (codePoint >> kContinuationBits));
            push(kContinuationTag | (codePoint & kContinuationPayload));
        } else if (codePoint < 0x10000) {
            push(0xE0 | (codePoint >> (2 * kContinuationBits)));
            push(kContinuationTag | ((codePoint >> kContinuationBits) & kContinuationPayload));
            push(kContinuationTag | (codePoint & kContinuationPayload));
        } else {
            push(0xF0 | (codePoint >> (3 * kContinuationBits)));
            push(kContinuationTag | ((codePoint >> (2 * kContinuationBits)) & kContinuationPayload));
            push(kContinuationTag | ((codePoint >> kContinuationBits) & kContinuationPayload));
            push(kContinuationTag | (codePoint & kContinuationPayload));
        }
    }

    size_t CountCodePoints(StringView text) {
        size_t count = 0;
        size_t pos = 0;
        while (pos < text.size()) {
            (void)DecodeNext(text, pos);
            ++count;
        }
        return count;
    }

}  // namespace BECore::Utf8
