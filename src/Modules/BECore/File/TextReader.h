#pragma once

#include <BECore/Types/Types.h>
#include <charconv>
#include <concepts>
#include <type_traits>

namespace BECore {

    /**
     * Lines, tokens and numbers of a text it does not own: the view has to outlive the reader and every view the reader returns.
     * Nothing here allocates. A call that finds nothing, or a number that is not a whole valid token, leaves the position where it was.
     *
     * White space is the C locale's: space, \t, \n, \v, \f, \r.
     */
    class TextReader {
    public:
        explicit constexpr TextReader(StringView text) noexcept : _text(text) {}

        /// Up to the next '\n' (not included), without a '\r' just before it; a last line without '\n' is a line; nothing at the end of the text
        [[nodiscard]] constexpr Optional<StringView> ReadLine() noexcept {
            if (AtEnd()) {
                return std::nullopt;
            }
            const StringView rest = Rest();
            const size_t newline = rest.find('\n');
            StringView line = newline == StringView::npos ? rest : rest.substr(0, newline);
            _position += newline == StringView::npos ? rest.size() : newline + 1;
            if (newline != StringView::npos && !line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            return line;
        }

        /// The next run of characters that are not white space, after the white space before it; nothing when only white space is left
        [[nodiscard]] constexpr Optional<StringView> ReadToken() noexcept {
            size_t begin = _position;
            while (begin < _text.size() && IsSpace(_text[begin])) {
                ++begin;
            }
            if (begin == _text.size()) {
                return std::nullopt;
            }
            size_t end = begin;
            while (end < _text.size() && !IsSpace(_text[end])) {
                ++end;
            }
            _position = end;
            return _text.substr(begin, end - begin);
        }

        /// The next token as a number of type T: the whole token must be one, in range for T; otherwise nothing and the position stays
        template <typename T>
            requires(std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char>) || std::floating_point<T> [[nodiscard]] Optional<T> Read() noexcept {
            TextReader probe = *this;
            const Optional<StringView> token = probe.ReadToken();
            if (!token) {
                return std::nullopt;
            }
            T value{};
            const char* const first = token->data();
            const char* const last = first + token->size();
            const auto [end, error] = std::from_chars(first, last, value);
            if (error != std::errc{} || end != last) {
                return std::nullopt;
            }
            *this = probe;
            return value;
        }

        template <std::integral T>
        [[nodiscard]] Optional<T> ReadInt() noexcept {
            return Read<T>();
        }

        template <std::floating_point T>
        [[nodiscard]] Optional<T> ReadFloat() noexcept {
            return Read<T>();
        }

        /// True when nothing is left, white space included
        [[nodiscard]] constexpr bool AtEnd() const noexcept {
            return _position >= _text.size();
        }

        /// What has not been read yet
        [[nodiscard]] constexpr StringView Rest() const noexcept {
            return _text.substr(_position);
        }

    private:
        static constexpr bool IsSpace(char c) noexcept {
            return c == ' ' || (c >= '\t' && c <= '\r');
        }

        StringView _text;
        size_t _position = 0;
    };

}  // namespace BECore
