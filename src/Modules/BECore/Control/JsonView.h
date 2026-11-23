#pragma once

#include <BECore/Control/JsonTape.h>

namespace BECore {

    class JsonView;

    /// A key and its value of an object, as `Members()` walks them
    struct JsonMember;

    namespace Detail {

        /// Walks the children of a container by the `next` of each token: items one by one, members as key tokens (their value follows the key)
        template <typename T, uint32_t Step>
        class JsonChildIterator {
        public:
            JsonChildIterator(const JsonTape* tape, uint32_t index) noexcept : _tape(tape), _index(index) {}

            T operator*() const;

            JsonChildIterator& operator++() noexcept {
                // A member's key token points at its value (`next`); the value's `next` is the following key
                _index = _tape->Tokens()[_index + Step].next;
                return *this;
            }

            bool operator!=(const JsonChildIterator& other) const noexcept {
                return _index != other._index;
            }

        private:
            const JsonTape* _tape;
            uint32_t _index;
        };

        template <typename T, uint32_t Step>
        class JsonChildRange {
        public:
            JsonChildRange(const JsonTape* tape, uint32_t first, uint32_t last) noexcept : _tape(tape), _first(first), _last(last) {}

            JsonChildIterator<T, Step> begin() const noexcept {
                return {_tape, _first};
            }
            JsonChildIterator<T, Step> end() const noexcept {
                return {_tape, _last};
            }

        private:
            const JsonTape* _tape;
            uint32_t _first;
            uint32_t _last;
        };

    }  // namespace Detail

    /**
     * @brief A cheap copyable cursor on one value of a JsonTape: the read-only document view where no reflected type exists
     *
     * Every read is checked: the wrong kind is `nullopt` / false / an empty range, never an assertion, because the text is a client's. A view
     * borrows the tape (and through it the text): it is valid while both live and the tape is not parsed again. A default view, or one over an
     * empty tape, is not valid and reads as nothing.
     *
     * A string is raw in the tape; Text() gives it with no copy when it holds no escape, ReadText() decodes it to UTF-8 into a caller's `String`.
     */
    class JsonView {
    public:
        JsonView() = default;

        /// The value at token @p index of @p tape; not valid when the tape has no such token. `JsonView(tape, 0)` is the root
        JsonView(const JsonTape& tape, uint32_t index) noexcept : _tape(index < tape.Size() ? &tape : nullptr), _index(index) {}

        [[nodiscard]] bool IsValid() const noexcept {
            return _tape != nullptr;
        }

        /// Null for a view that is not valid; tell the two apart with IsValid()
        [[nodiscard]] JsonKind Kind() const noexcept {
            return IsValid() ? Token().kind : JsonKind::Null;
        }

        [[nodiscard]] bool IsNull() const noexcept {
            return IsValid() && Kind() == JsonKind::Null;
        }
        [[nodiscard]] bool IsBool() const noexcept {
            return IsValid() && Kind() == JsonKind::Bool;
        }
        /// An Integer or a Number token
        [[nodiscard]] bool IsNumber() const noexcept {
            return IsValid() && (Kind() == JsonKind::Integer || Kind() == JsonKind::Number);
        }
        /// A number with no fraction and no exponent (it may still not fit 64 bits)
        [[nodiscard]] bool IsInteger() const noexcept {
            return IsValid() && Kind() == JsonKind::Integer;
        }
        [[nodiscard]] bool IsString() const noexcept {
            return IsValid() && Kind() == JsonKind::String;
        }
        [[nodiscard]] bool IsArray() const noexcept {
            return IsValid() && Kind() == JsonKind::Array;
        }
        [[nodiscard]] bool IsObject() const noexcept {
            return IsValid() && Kind() == JsonKind::Object;
        }

        /// Items of an array or members of an object; 0 for any other value
        [[nodiscard]] size_t Size() const noexcept {
            return (IsArray() || IsObject()) ? Token().count : 0;
        }

        /// The tape this view reads, nullptr for a view that is not valid: what a reader that is handed a sub-document needs to walk it
        [[nodiscard]] const JsonTape* Tape() const noexcept {
            return _tape;
        }

        /// The token this view is on, and the index after its subtree: what a reader that walks the tape itself keeps as a cursor
        [[nodiscard]] uint32_t Index() const noexcept {
            return _index;
        }
        [[nodiscard]] uint32_t End() const noexcept {
            return IsValid() ? Token().next : _index;
        }

        /// The text of the value as written; a string with its quotes, a container whole. Empty for a view that is not valid
        [[nodiscard]] StringView Raw() const noexcept;

        /// The member called @p key of an object (the first of that name), found among all members; a key written with escapes matches its decoded form
        [[nodiscard]] Optional<JsonView> Find(StringView key) const noexcept;

        /**
         * @brief Find() that starts at the member after the last match and wraps once
         *
         * @p cursor is a token index the caller keeps per object, 0 at the start; a document that keeps the order the reader asks costs one
         * key compare per read. It moves past the match; a miss leaves it.
         */
        [[nodiscard]] Optional<JsonView> Find(StringView key, uint32_t& cursor) const noexcept;

        /// The items of an array, in order; empty for any other value
        [[nodiscard]] Detail::JsonChildRange<JsonView, 0> Items() const noexcept;

        /// The members of an object, in the order of the text; empty for any other value
        [[nodiscard]] Detail::JsonChildRange<JsonMember, 1> Members() const noexcept;

        [[nodiscard]] Optional<bool> AsBool() const noexcept;

        /// An Integer token that fits; `-0` is 0
        [[nodiscard]] Optional<int64_t> AsInt64() const noexcept;
        [[nodiscard]] Optional<uint64_t> AsUint64() const noexcept;

        /**
         * @brief An Integer or a Number token as a double; nullopt for another kind or a text that does not convert
         *
         * A number beyond the double range is infinite, not nullopt: whether that is acceptable is the caller's check (`std::isfinite`).
         */
        [[nodiscard]] Optional<double> AsDouble() const noexcept;

        /// A string that holds no escape, as a view into the text (an embedded NUL stays: it is sized); nullopt for a string with an escape
        /// or another kind
        [[nodiscard]] Optional<StringView> Text() const noexcept;

        /// Decodes a string to UTF-8 into @p out (assigned, its capacity reused); false and @p out untouched for another kind
        bool ReadText(String& out) const;

        /// Whether this is a string whose decoded text is @p text; no buffer, so a key with escapes costs no allocation
        [[nodiscard]] bool TextEquals(StringView text) const noexcept;

    private:
        const JsonToken& Token() const noexcept {
            return _tape->Tokens()[_index];
        }

        const JsonTape* _tape = nullptr;
        uint32_t _index = 0;
    };

    struct JsonMember {
        JsonView key;
        JsonView value;
    };

    namespace Detail {

        template <typename T, uint32_t Step>
        T JsonChildIterator<T, Step>::operator*() const {
            if constexpr (Step == 0) {
                return JsonView(*_tape, _index);
            } else {
                return JsonMember{JsonView(*_tape, _index), JsonView(*_tape, _index + 1)};
            }
        }

    }  // namespace Detail

}  // namespace BECore
