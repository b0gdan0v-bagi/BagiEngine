#pragma once

namespace BECore::UIWebText {

    [[nodiscard]] bool IsSpace(char c);

    /// @p text without its leading and trailing white space (spaces, tabs and line breaks)
    [[nodiscard]] eastl::string_view TrimSpace(eastl::string_view text);

    [[nodiscard]] eastl::string ToLower(eastl::string_view text);

    /// The parts of @p text between @p separator characters that are outside parentheses and quotes, trimmed. Empty parts are kept
    [[nodiscard]] eastl::vector<eastl::string_view> SplitTopLevel(eastl::string_view text, char separator);

    /// The words of @p text: runs of non-space characters, where a space inside parentheses or quotes does not end a word (`rgba(0, 0, 0, .5)` is one)
    [[nodiscard]] eastl::vector<eastl::string_view> SplitWords(eastl::string_view text);

    /// A CSS number with an optional `px` (`12`, `12px`, `.5`, `-3`). Other units give nullopt
    [[nodiscard]] std::optional<float> ParsePixels(eastl::string_view text);

    /// A CSS color: `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()` / `rgba()` (commas or spaces, alpha as a number 0 to 1 or a percent), `transparent` and a few names
    [[nodiscard]] std::optional<Color> ParseColor(eastl::string_view text);

    /// The content of `url(...)` with its quotes removed; nullopt when @p text is not a url
    [[nodiscard]] std::optional<eastl::string> ParseUrl(eastl::string_view text);

}  // namespace BECore::UIWebText
