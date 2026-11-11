#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /**
     * @brief The text forms of the small value types a UI file holds in one attribute: `width="90%"`, `margin="8"`, `color="0,0,0,160"`.
     *
     * Pure functions. Every Format result parses back to the same value, and a Parse of malformed text gives nullopt, so a loader can report the attribute.
     */
    namespace UIText {

        /// "220" for units, "90%" for a percent
        [[nodiscard]] eastl::string FormatLength(const UILength& length);
        /// "220", "220px" or "90%"
        [[nodiscard]] std::optional<UILength> ParseLength(eastl::string_view text);

        /// "x,y": two plain fractions, the text form of an anchor or a pivot (a percent is written as the fraction, so 50% is 0.5)
        [[nodiscard]] eastl::string FormatFraction2(const Vec2& value);
        /// "x,y", each a number or `N%` (10% is 0.1); both finite
        [[nodiscard]] std::optional<Vec2> ParseFraction2(eastl::string_view text);

        /// One number when the four edges are equal, otherwise "left,top,right,bottom"
        [[nodiscard]] eastl::string FormatEdges(const UIEdges& edges);
        /// One number (all edges) or "left,top,right,bottom"
        [[nodiscard]] std::optional<UIEdges> ParseEdges(eastl::string_view text);

        /// "r,g,b,a"
        [[nodiscard]] eastl::string FormatColor(const Color& color);
        /// "r,g,b,a" (0 to 255 each), "#RRGGBB" or "#RRGGBBAA"
        [[nodiscard]] std::optional<Color> ParseColor(eastl::string_view text);

        /// A colour as a file writes it: a literal, or a reference to a theme token (`@primary`, UIThemes)
        struct ColorRef {
            Color color;       ///< the literal; transparent for a token (its colour is the theme's)
            PoolString token;  ///< empty: a literal
        };

        /// "@name" for a non-empty @p token (the colour is ignored), else FormatColor()
        [[nodiscard]] eastl::string FormatColorRef(const Color& color, PoolString token);
        /// Any form of ParseColor(), or "@name" with a legal token name (UIThemes::IsValidToken) that is interned into the pool of names. Nothing for anything else
        [[nodiscard]] std::optional<ColorRef> ParseColorRef(eastl::string_view text);

        /// One radius when the four corners are equal, otherwise "topLeft,topRight,bottomRight,bottomLeft"
        [[nodiscard]] eastl::string FormatCorners(const UICorners& corners);
        /// One number (all corners) or "topLeft,topRight,bottomRight,bottomLeft"; every number finite
        [[nodiscard]] std::optional<UICorners> ParseCorners(eastl::string_view text);

        /// "offsetX,offsetY,blur,spread,#RRGGBBAA", or with "@name" instead of the colour for a non-empty @p colorToken
        [[nodiscard]] eastl::string FormatShadow(const UIShadowSpec& shadow, PoolString colorToken = {});
        /// "offsetX,offsetY,blur,spread,color", the colour in any form of ParseColor (so also "r,g,b,a": the first four commas split the numbers, the rest is the colour). The numbers
        /// are finite and the blur is not negative. A colour "@name" is refused unless @p colorToken is given: it gets the name and the spec's colour stays transparent (a literal
        /// colour sets it empty)
        [[nodiscard]] std::optional<UIShadowSpec> ParseShadow(eastl::string_view text, PoolString* colorToken = nullptr);

        /// "width,#RRGGBBAA": the outline of a text in UI units ("@name" for a non-empty @p token)
        [[nodiscard]] eastl::string FormatTextOutline(float width, const Color& color, PoolString token = {});
        /// "width,color", the colour in any form of ParseColor (the first comma splits the width, the rest is the colour); the width is finite and not negative. "@name" as the
        /// colour is refused unless @p token is given (see ParseShadow)
        [[nodiscard]] bool ParseTextOutline(eastl::string_view text, float& width, Color& color, PoolString* token = nullptr);

        /// "offsetX,offsetY,#RRGGBBAA": the hard shadow of a text in UI units ("@name" for a non-empty @p token)
        [[nodiscard]] eastl::string FormatTextShadow(float offsetX, float offsetY, const Color& color, PoolString token = {});
        /// "offsetX,offsetY,color", the colour in any form of ParseColor (the first two commas split the numbers); the numbers are finite. "@name" as the colour is refused unless
        /// @p token is given (see ParseShadow)
        [[nodiscard]] bool ParseTextShadow(eastl::string_view text, float& offsetX, float& offsetY, Color& color, PoolString* token = nullptr);

        /// A number that fills the whole text (surrounding spaces are allowed)
        [[nodiscard]] std::optional<float> ParseFloat(eastl::string_view text);

        /// Like ParseFloat, but "nan", "inf" and "-inf" are refused: a number a control can compute with
        [[nodiscard]] std::optional<float> ParseFiniteFloat(eastl::string_view text);

        /// Where @p value stands in @p minValue..@p maxValue: `clamp((value - minValue) / (maxValue - minValue), 0, 1)`. 0 for a range that is empty or reversed
        /// (`maxValue <= minValue`) and for any number that is not finite, so a caller never divides by zero or draws outside its rect. The one place of these rules, shared by the
        /// progress bar and the slider
        [[nodiscard]] float FractionInRange(float value, float minValue, float maxValue);

        /// The number a variable's @p text stands for, clamped to @p minValue..@p maxValue; @p minValue for an empty or malformed text, a number that is not finite and a range that is empty or
        /// reversed
        [[nodiscard]] float ValueInRange(eastl::string_view text, float minValue, float maxValue);

        /// @p text without its leading and trailing spaces
        [[nodiscard]] eastl::string_view Trim(eastl::string_view text);

    }  // namespace UIText

}  // namespace BECore
