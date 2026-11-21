#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief A streaming JSON writer that appends to a caller's String: no DOM, no temporary string.
     *
     * The writer allocates nothing itself; only the growth of the output String can, so a message of the same shape written into a String
     * that kept its capacity allocates nothing. The text is byte-identical to `nlohmann::json::dump(indent, ' ', false, error_handler_t::replace)`
     * of the same document: the escapes, the replacement of invalid UTF-8, the integers, `float` and `double` text, `null` for a value that is
     * not finite, and the layout of both the compact and the indented form.
     *
     * Commas, the colon and the indentation are the writer's. The `Json` DOM keeps its object keys in alphabetical order, so a message written by
     * hand writes its keys in that order too when its bytes have to stay the same as the DOM path's (Control/CLAUDE.md, "JSON rules").
     *
     * Misuse (a Begin past kMaxDepth, an End with nothing open or of the other kind, a Key outside an object or while a value is due, a Value in
     * an object without a Key, a second root value) is a bug of the engine's own code, never of a client: it is an `EXPECT`, the call is ignored
     * and IsValid() goes false. The text written after a misuse is not valid JSON. It never aborts the process.
     */
    class JsonWriter {
    public:
        /// The indent that writes the compact form, like `dump(-1)`
        static constexpr int kCompact = -1;

        /// Containers open at once; kept in bit flags of the object, so nesting never allocates
        static constexpr size_t kMaxDepth = 256;

        /// Appends to @p out and never clears it. @p indent 0 or more is the layout of `dump(indent)`: a newline and @p indent spaces per level,
        /// `"key": value`, `{}` and `[]` for an empty container; kCompact is `dump(-1)`.
        explicit JsonWriter(String& out, int indent = kCompact);

        JsonWriter(const JsonWriter&) = delete;
        JsonWriter& operator=(const JsonWriter&) = delete;

        void BeginObject();
        void EndObject();
        void BeginArray();
        void EndArray();

        /// A member name of the open object, escaped like a string value
        void Key(StringView key);

        /**
         * @brief A member name that is ready text: the quoted, already escaped name and its colon, `"text":`.
         *
         * For a generator that holds the name as a constant. With an indent the writer adds the space after the colon itself, so the same literal serves
         * both forms; text that is not `"..."` followed by `:` is a misuse.
         */
        void KeyLiteral(StringView ready);

        void Value(bool value);
        void Value(float value);
        void Value(double value);
        void Value(StringView value);
        void Value(const PoolString& value);
        void Value(const char* value);

        /// A pointer other than `const char*` would turn into a bool without a word
        template <typename T>
        void Value(const T*) = delete;

        /// Any integer up to 64 bits; a character type is not a number here
        template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t> && !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> &&
                 !std::is_same_v<T, char32_t> && sizeof(T) <= sizeof(uint64_t)) void Value(T value) {
            if constexpr (std::is_signed_v<T>) {
                WriteSigned(static_cast<int64_t>(value));
            } else {
                WriteUnsigned(static_cast<uint64_t>(value));
            }
        }

        void Null();

        /// One value that is ready JSON text; it is copied as it is, also in the indented form
        void Raw(StringView ready);

        /// Key + Value
        template <typename T>
        void Member(StringView key, T&& value) {
            Key(key);
            Value(std::forward<T>(value));
        }

        /// False after the first misuse
        [[nodiscard]] bool IsValid() const {
            return _valid;
        }

        /// One root value is written, nothing is open and nothing went wrong
        [[nodiscard]] bool IsComplete() const {
            return _valid && _rootDone && _depth == 0;
        }

    private:
        static constexpr size_t kBitsPerWord = 64;
        static constexpr size_t kFlagWords = kMaxDepth / kBitsPerWord;

        /// Writes what precedes a value (the comma and the indent in an array) and checks that a value is allowed here
        [[nodiscard]] bool BeginValue();
        void EndValue();
        void Begin(char open, bool isObject);
        void End(char close, bool isObject);
        void WriteKeyPrefix();
        void NewLine(size_t depth);
        void AppendEscaped(StringView text);
        void WriteSigned(int64_t value);
        void WriteUnsigned(uint64_t value);
        void WriteDouble(double value);
        bool Fail(const char* why);

        [[nodiscard]] bool InObject() const;
        [[nodiscard]] bool GetFlag(const Array<uint64_t, kFlagWords>& flags, size_t level) const;
        void SetFlag(Array<uint64_t, kFlagWords>& flags, size_t level, bool value);

        String& _out;
        int _indent;
        size_t _depth = 0;
        Array<uint64_t, kFlagWords> _isObject{};  // per open level: an object, else an array
        Array<uint64_t, kFlagWords> _hasItems{};  // per open level: something was written, so the next item needs a comma
        bool _keyPending = false;                 // a Key of the open object waits for its value
        bool _rootDone = false;
        bool _valid = true;
    };

}  // namespace BECore
