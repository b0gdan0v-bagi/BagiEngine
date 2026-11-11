#pragma once

#include <BECore/UI/UIBoxMesh.h>

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

    /// A colour of a style sheet: a literal, or a reference to a theme token (`var(--name)`, written `@name` in the generated XML, `UIThemes`). For a token the channels are
    /// transparent: the theme gives them when the element is drawn. It is a Color for everything that reads a literal
    struct WebColor : Color {
        PoolString token;  ///< the name of the theme token; empty: a literal colour

        WebColor() = default;
        WebColor(const Color& literal) : Color(literal) {}  // NOLINT(google-explicit-constructor): a literal colour is a WebColor

        [[nodiscard]] bool IsToken() const {
            return !token.Empty();
        }

        /// Whether the colour paints anything: a token does (its theme colour is not known here), a literal does with a non-zero alpha
        [[nodiscard]] bool IsVisible() const {
            return IsToken() || a != 0;
        }
    };

    /// The attribute text of @p color: `@name` for a token, else `r,g,b,a`
    [[nodiscard]] eastl::string FormatWebColor(const WebColor& color);

    /// A CSS color: `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()` / `rgba()` (commas or spaces, alpha as a number 0 to 1 or a percent), `transparent` and a few names; or `var(--name)`
    /// with a legal token name (`UIThemes::IsValidToken`) and **no fallback value**, which is a token. Nothing for anything else (`var(--name, red)` included)
    [[nodiscard]] std::optional<WebColor> ParseColor(eastl::string_view text);

    /// The content of `url(...)` with its quotes removed; nullopt when @p text is not a url
    [[nodiscard]] std::optional<eastl::string> ParseUrl(eastl::string_view text);

    /// The corners of a `border-radius` value: one to four px lengths in the CSS order (one for all, two for top-left+bottom-right and top-right+bottom-left, three,
    /// four) as a `UICorners`. `%`, other units, a `/` (elliptical corners) and a negative length give nullopt
    [[nodiscard]] std::optional<UICorners> ParseCornerRadii(eastl::string_view text);

    /// The border of a box as the `border` shorthand gives it
    struct BorderSpec {
        float width = 0.0f;  ///< px; 0 when there is no border
        WebColor color;      ///< unset (transparent black) when there is no border
        bool none = false;   ///< `none` or a zero width: no border
    };

    /// A `border` value: a px width, the style `solid` or `none` and a color, in any order. The style may be left out (it is `solid`); the color may be left out only
    /// for `none` or a zero width. `none` or a zero width is no border. Another style (`dashed`, `dotted`...), a negative width, a repeated part and an
    /// unknown word give nullopt
    [[nodiscard]] std::optional<BorderSpec> ParseBorder(eastl::string_view text);

    /// A `linear-gradient()` of two colors, as the box attributes want it: the direction, the start (top or left edge) and the end colors
    struct GradientSpec {
        UIGradientDirection direction = UIGradientDirection::Vertical;
        WebColor start;
        WebColor end;
    };

    /// A `linear-gradient(...)`: an optional direction (`to bottom`, `to right`, `to top`, `to left`, `180deg`, `90deg`, `0deg`, `270deg`; to bottom when left out)
    /// and exactly two color stops with no positions. `to top` / `0deg` and `to left` / `270deg` run against the axis of the attribute, so they swap the colors.
    /// Another angle, a corner direction, one or three stops and a stop position give nullopt
    [[nodiscard]] std::optional<GradientSpec> ParseLinearGradient(eastl::string_view text);

    /// The layers a `box-shadow` can hold in the engine: the glow and the shadow of a box
    inline constexpr size_t MaxShadowLayers = 2;

    /// The layers of a `box-shadow`, in the order of the text (the first is painted on top in CSS)
    struct BoxShadows {
        eastl::array<UIShadowSpec, MaxShadowLayers> layers;  ///< the first `min(count, MaxShadowLayers)` are set; the colour of a layer with a token is transparent
        eastl::array<PoolString, MaxShadowLayers> tokens;    ///< per layer, the theme token of its colour; empty for a literal (UIShadowSpec is a mesh spec and holds none)
        size_t count = 0;                                    ///< the layers in the text, also those past MaxShadowLayers (the caller reports them)
    };

    /// A `box-shadow`: `none` (no layers) or comma-separated layers `offset-x offset-y [blur [spread]] color` (px; the color first or last). A negative blur, `inset`,
    /// a missing color or a bad value in any layer, also one past the limit, gives nullopt
    [[nodiscard]] std::optional<BoxShadows> ParseBoxShadows(eastl::string_view text);

}  // namespace BECore::UIWebText
