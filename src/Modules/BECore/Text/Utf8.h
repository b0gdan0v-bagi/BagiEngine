#pragma once

namespace BECore::Utf8 {

    /// What an invalid byte sequence decodes to
    constexpr char32_t kReplacement = 0xFFFD;

    /// Decodes the code point that starts at @p pos and moves @p pos past it. A malformed sequence (a stray continuation
    /// byte, a truncated or overlong sequence, a surrogate, a value past U+10FFFF) gives kReplacement and consumes one byte,
    /// so a loop over any bytes always ends. Returns 0 and leaves @p pos alone when @p pos is at or past the end.
    [[nodiscard]] char32_t DecodeNext(eastl::string_view text, size_t& pos);

    /// Appends @p codePoint as UTF-8; a value that is not a Unicode scalar value appends kReplacement
    void Append(eastl::string& out, char32_t codePoint);

    /// The number of code points in @p text (malformed bytes count one each, like DecodeNext)
    [[nodiscard]] size_t CountCodePoints(eastl::string_view text);

}  // namespace BECore::Utf8
