#pragma once

#include <BECore/UI/UIElement.h>

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
    };

    /// The properties of one named element that a variant replaces; an unset one is left as it is
    struct UIElementOverride {
        PoolString element;
        std::optional<bool> visible;
        std::optional<UIAnchor> anchor;
        std::optional<float> x;
        std::optional<float> y;
        std::optional<UILength> width;
        std::optional<UILength> height;
        std::optional<UIStackDirection> direction;
        std::optional<float> fontSize;

        void ApplyTo(UIElementStyle& style) const;
    };

    /// A responsive variant: while `when` holds for the canvas bounds, `overrides` are in effect
    struct UIVariant {
        UIVariantCondition when;
        eastl::vector<UIElementOverride> overrides;
    };

    class UIVariants {
    public:
        /**
         * @brief Puts into `active` of every element of the tree under @p root the authored `style` with the matching variants applied.
         *
         * Variants apply in the order of the list, so a later one wins where two set the same property, like CSS. An override names an element;
         * one that names none is ignored. Always starts from `style`, so a shape that no variant matches any more comes back.
         * @return how many variants matched
         */
        static size_t Apply(UIElement& root, eastl::span<const UIVariant> variants, const Rect& bounds);
    };

}  // namespace BECore
