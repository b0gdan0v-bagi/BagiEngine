#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <expected>

namespace BECore {

    CORE_ENUM(JsonKind, uint8_t, Null, Bool, Integer, Number, String, Array, Object)

    /// Why a text is not one JSON value: a literal message and the byte offset it was found at
    struct JsonParseError {
        size_t offset = 0;
        StringView message;
    };

    /// One value of a JsonTape. A string's offset and length are of the content between the quotes (the escapes are not decoded), a scalar's of
    /// its text (`true`, `-12`, `1.5e3`), a container's of its whole span from `{` / `[` to the closing bracket (so its raw text is a view).
    /// `next` is the index after the subtree (a scalar: `index + 1`); `count` is the members or items of a container.
    struct JsonToken {
        /// Set in `flags` of a String that holds a backslash
        static constexpr uint8_t kHasEscape = 1;

        JsonKind kind = JsonKind::Null;
        uint8_t flags = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
        uint32_t next = 0;
        uint32_t count = 0;
    };

    /**
     * @brief The tokens of one JSON text, made in one pass with no recursion: the document the JSON readers walk instead of a DOM.
     *
     * The grammar is RFC 8259: a single value with whitespace around it; no trailing comma, leading zero,
     * `+`, `NaN` or comment; no control character in a string; only the legal escapes, a `\u` surrogate pair joined and a lone surrogate refused;
     * UTF-8 in a string checked. The differences are the nesting (at most ReaderLimits::kMaxJsonNesting open containers), the size
     * (ReaderLimits::kMaxJsonDocumentBytes) and a UTF-8 byte order mark, which is refused.
     *
     * The tokens are in document order: an object is its token and then a String key token and its value, key after key (the key's `next` is
     * the index of its value). A number without a fraction or an exponent is an Integer, every other is a Number; the tape does not judge
     * whether it fits 64 bits or a double, the reader that knows the target does.
     *
     * **The tape borrows the text**: the text must stay alive and unchanged while the tape, a view of it or a read of it is used. Parse() clears and
     * refills buffers the object keeps, so one tape serves many messages, and a text of a shape the tape has seen allocates nothing. It never
     * throws or aborts, whatever the text; a text that does not parse leaves the tape empty.
     */
    class JsonTape {
    public:
        /// Tokenizes @p text (not copied). On an error the tape is empty.
        [[nodiscard]] std::expected<void, JsonParseError> Parse(StringView text);

        [[nodiscard]] const Vector<JsonToken>& Tokens() const noexcept {
            return _tokens;
        }

        /// The text the tokens point into
        [[nodiscard]] StringView Text() const noexcept {
            return _text;
        }

        [[nodiscard]] size_t Size() const noexcept {
            return _tokens.size();
        }

        [[nodiscard]] bool Empty() const noexcept {
            return _tokens.empty();
        }

    private:
        StringView _text;
        Vector<JsonToken> _tokens;
        /// Token indices of the containers still open while parsing
        Vector<uint32_t> _open;
    };

    /// True when `[` and `{` outside strings nest deeper than @p maxDepth. A scan that does not recurse and does not judge the syntax (the parser does).
    /// The tape needs no such check (it is iterative and stops at ReaderLimits::kMaxJsonNesting); a transport uses it to refuse a deep request before
    /// anything else reads it, because a reader that recurses once per level can overflow a reader thread's stack
    [[nodiscard]] bool IsJsonNestedDeeperThan(StringView text, size_t maxDepth);

}  // namespace BECore
