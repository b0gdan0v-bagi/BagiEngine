#include "JsonPointerSyntax.h"

namespace BECore::Detail {

    namespace {

        constexpr char kPointerEscape = '~';

    }  // namespace

    ControlError BadPointer(StringView why) {
        return MakeControlError(ControlErrors::kInvalidParams, Format("bad JSON pointer: {}", why));
    }

    Optional<ControlError> CheckPointerEscapes(StringView pointer) {
        for (size_t i = 0; i < pointer.size(); ++i) {
            if (pointer[i] != kPointerEscape) {
                continue;
            }
            const bool valid = i + 1 < pointer.size() && (pointer[i + 1] == '0' || pointer[i + 1] == '1');
            if (!valid) {
                return BadPointer("`~` must be followed by 0 or 1");
            }
            ++i;
        }
        return std::nullopt;
    }

    StringView UnescapePointerToken(StringView token, String& storage) {
        if (token.find(kPointerEscape) == StringView::npos) {
            return token;
        }
        storage.clear();
        for (size_t i = 0; i < token.size(); ++i) {
            if (token[i] == kPointerEscape) {
                ++i;
                storage.push_back(token[i] == '0' ? kPointerEscape : kPointerSeparator);
            } else {
                storage.push_back(token[i]);
            }
        }
        return StringView(storage.c_str(), storage.size());
    }

    std::expected<Optional<size_t>, ControlError> ParseArrayIndex(StringView token, size_t size) {
        if (token == "-") {
            return std::unexpected(BadPointer("`-` names the element after the last one, which cannot be read"));
        }
        if (token.empty() || (token.size() > 1 && token[0] == '0')) {
            return std::unexpected(BadPointer("an array index is 0 or digits without a leading zero"));
        }
        size_t index = 0;
        bool pastEnd = false;
        for (const char c : token) {
            if (c < '0' || c > '9') {
                return std::unexpected(BadPointer("an array index is 0 or digits without a leading zero"));
            }
            // Digits keep being checked after the index left the array, so a bad token is an error whatever its length
            if (!pastEnd) {
                index = index * 10 + static_cast<size_t>(c - '0');
                pastEnd = index >= size;
            }
        }
        return pastEnd ? Optional<size_t>() : Optional<size_t>(index);
    }

}  // namespace BECore::Detail
