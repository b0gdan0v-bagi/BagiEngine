#pragma once

#include <fmt/format.h>

// The part of the format layer that a type teaches how to be formatted (Format/CLAUDE.md). It is a header of its own because
// EnumUtils.h, which comes before Format.h in the PCH, makes every CORE_ENUM formattable through it.
namespace BECore {

    /// Where a formatter writes the text, and what it reads the spec (`{:>8}`) from.
    using FormatContext = fmt::format_context;
    using FormatParseContext = fmt::format_parse_context;

    /**
     * @brief How a type is formatted: specialize it in namespace BECore, before the first format call that names the type.
     *
     * A specialization has two members and names no fmt::
     *   constexpr FormatParseContext::iterator Parse(FormatParseContext& ctx);          // reads the spec, returns the end of it
     *   FormatContext::iterator Format(const T& value, FormatContext& ctx) const;       // writes the value, returns the end of it
     * Parse must be constexpr: the format string of a call is checked at compile time through it.
     * A specialization declared after the type was first used in a format call is not seen (an incomplete type cannot be detected later).
     */
    template <typename T, typename Enable = void>
    struct Formatter;

    /// True when a complete Formatter<T> exists.
    template <typename T>
    concept HasFormatter = requires {
        sizeof(Formatter<T>);
    };

    /**
     * @brief The base of a Formatter for a type written as text: reads the usual string spec (`{:>8}`, `{:.3}`) and writes a StringView.
     * The type's own Format(const T&, FormatContext&) const turns the value into a StringView and calls FormatText.
     */
    struct TextFormatter {
        constexpr FormatParseContext::iterator Parse(FormatParseContext& ctx) {
            return _spec.parse(ctx);
        }

        FormatContext::iterator FormatText(StringView text, FormatContext& ctx) const {
            return _spec.format(ToStd(text), ctx);
        }

    private:
        fmt::formatter<std::string_view> _spec;  //-NoStd: the spec parser of fmt works on std::string_view
    };

}  // namespace BECore

/// The one bridge from the engine's Formatter<T> to fmt: every type with a complete Formatter<T> is formattable by fmt through it.
template <typename T>
requires BECore::HasFormatter<T> struct fmt::formatter<T, char> {
    constexpr auto parse(BECore::FormatParseContext& ctx) {
        return _impl.Parse(ctx);
    }

    auto format(const T& value, BECore::FormatContext& ctx) const {
        return _impl.Format(value, ctx);
    }

private:
    BECore::Formatter<T> _impl;
};
