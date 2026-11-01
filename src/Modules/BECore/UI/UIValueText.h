#pragma once

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

        /// One number when the four edges are equal, otherwise "left,top,right,bottom"
        [[nodiscard]] eastl::string FormatEdges(const UIEdges& edges);
        /// One number (all edges) or "left,top,right,bottom"
        [[nodiscard]] std::optional<UIEdges> ParseEdges(eastl::string_view text);

        /// "r,g,b,a"
        [[nodiscard]] eastl::string FormatColor(const Color& color);
        /// "r,g,b,a" (0 to 255 each), "#RRGGBB" or "#RRGGBBAA"
        [[nodiscard]] std::optional<Color> ParseColor(eastl::string_view text);

        /// A number that fills the whole text (surrounding spaces are allowed)
        [[nodiscard]] std::optional<float> ParseFloat(eastl::string_view text);

        /// @p text without its leading and trailing spaces
        [[nodiscard]] eastl::string_view Trim(eastl::string_view text);

    }  // namespace UIText

}  // namespace BECore
