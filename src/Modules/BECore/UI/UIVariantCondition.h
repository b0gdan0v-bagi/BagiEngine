#pragma once

namespace BECore {

    /// Portrait is a canvas at least as tall as it is wide (a square is portrait, like CSS), landscape is wider than tall
    CORE_ENUM(UIOrientation, uint8_t, Any, Portrait, Landscape)

    /**
     * @brief A condition on the bounds of a canvas in UI units. Every part that is set must hold.
     *
     * A limit of 0 is not set. The aspect is width / height; the ranges include their ends.
     */
    struct UIVariantCondition {
        UIOrientation orientation = UIOrientation::Any;
        float minAspect = 0.0f;
        float maxAspect = 0.0f;
        float minWidth = 0.0f;
        float maxWidth = 0.0f;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;

        [[nodiscard]] bool Matches(const Rect& bounds) const;

        /// The set parts as "name:value" separated by ';' (`orientation:portrait;minAspect:1.5`), in the order of the fields; empty when nothing is set
        [[nodiscard]] eastl::string ToText() const;

        /// Reads the form of ToText(); the names and `portrait` / `landscape` / `any` are case-insensitive. The error names the part that was not understood
        [[nodiscard]] static std::expected<UIVariantCondition, eastl::string> FromText(eastl::string_view text);

        [[nodiscard]] bool operator==(const UIVariantCondition&) const = default;
    };

}  // namespace BECore
