#pragma once

// The string types of the engine. Code names these, not eastl:: / std: (Types/CLAUDE.md): the implementation is chosen here, once.
namespace BECore {

    /// The owning string. Today eastl::string; an alias so that the allocator or the type can change in one place.
    using String = eastl::string;

    /// The non-owning view of characters. Today eastl::string_view.
    using StringView = eastl::string_view;

    /// A string with N characters inside the object; with Overflow it moves to the heap when longer, without it the length is capped at N.
    template <size_t N, bool Overflow = true>
    using FixedString = eastl::fixed_string<char, N, Overflow>;

    /// Hash of a string key that takes a String, a StringView or a const char* without making a String: the hash map of String keys
    /// looks up by view or literal (`map.find("name")`). The value is the one eastl::hash<eastl::string> gives for a string without
    /// a NUL character, so the iteration order of a map does not depend on which of the two hashes it.
    struct StringHash {
        using is_transparent = void;

        size_t operator()(StringView text) const noexcept {
            return eastl::hash<StringView>{}(text);
        }

        size_t operator()(const char* text) const noexcept {
            return (*this)(StringView{text});
        }
    };

    /// Equality of a string key and a String, StringView or const char* (see StringHash).
    struct StringEqual {
        using is_transparent = void;

        constexpr bool operator()(StringView lhs, StringView rhs) const noexcept {
            return lhs == rhs;
        }
    };

    /// Order of a string key and a String, StringView or const char* (the transparent comparison of an ordered map of Strings).
    struct StringLess {
        using is_transparent = void;

        constexpr bool operator()(StringView lhs, StringView rhs) const noexcept {
            return lhs < rhs;
        }
    };

    /// The number as text, as std::to_string writes it (an integer: its digits; a floating-point value: `%f`). Characters and bool
    /// are not numbers here: they have no overload, so a call with one does not compile.
    template <typename T>
    requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t> && !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> &&
             !std::is_same_v<T, char32_t>) [[nodiscard]] String ToString(T value) {
        if constexpr (std::is_integral_v<T>) {
            char digits[24];
            const auto result = std::to_chars(digits, digits + sizeof(digits), value);
            return String{digits, static_cast<size_t>(result.ptr - digits)};
        } else {
            const std::string text = std::to_string(value);
            return String{text.data(), text.size()};
        }
    }

    // The boundary with std:: and third-party APIs. The views are constexpr and copy nothing; the pair that makes a string copies and
    // is named so. Use them at the call, not as a habit: a `std::` type inside the engine's own code goes through these only.

    /// The same characters as a std::string_view (for std::filesystem, std::from_chars, nlohmann, SDL wrappers, ...).
    [[nodiscard]] constexpr std::string_view ToStd(StringView view) noexcept {
        return std::string_view{view.data(), view.size()};
    }

    /// The same characters as a StringView.
    [[nodiscard]] constexpr StringView FromStd(std::string_view view) noexcept {
        return StringView{view.data(), view.size()};
    }

    /// A copy as a std::string.
    [[nodiscard]] inline std::string ToStdString(StringView view) {
        return std::string{view.data(), view.size()};
    }

    /// A copy as a String.
    [[nodiscard]] inline String FromStdString(std::string_view view) {
        return String{view.data(), view.size()};
    }

}  // namespace BECore
