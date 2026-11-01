#include "UIVariant.h"

namespace BECore {

    namespace {

        bool InRange(float value, float minimum, float maximum) {
            return (minimum <= 0.0f || value >= minimum) && (maximum <= 0.0f || value <= maximum);
        }

        void ResetActive(UIElement& element) {
            element.active = element.style;
            for (const auto& child : element.children) {
                ResetActive(*child);
            }
        }

        void ApplyOverride(UIElement& element, const UIElementOverride& override) {
            if (element.name == override.element) {
                override.ApplyTo(element.active);
            }
            for (const auto& child : element.children) {
                ApplyOverride(*child, override);
            }
        }

    }  // namespace

    bool UIVariantCondition::Matches(const Rect& bounds) const {
        const bool portrait = bounds.h >= bounds.w;
        if ((orientation == UIOrientation::Portrait && !portrait) || (orientation == UIOrientation::Landscape && portrait)) {
            return false;
        }
        if ((minAspect > 0.0f || maxAspect > 0.0f) && bounds.h <= 0.0f) {
            return false;
        }
        const float aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 0.0f;
        return InRange(aspect, minAspect, maxAspect) && InRange(bounds.w, minWidth, maxWidth) && InRange(bounds.h, minHeight, maxHeight);
    }

    void UIElementOverride::ApplyTo(UIElementStyle& style) const {
        if (visible) {
            style.visible = *visible;
        }
        if (anchor) {
            style.rect.anchor = *anchor;
        }
        if (x) {
            style.rect.x = *x;
        }
        if (y) {
            style.rect.y = *y;
        }
        if (width) {
            style.rect.width = *width;
        }
        if (height) {
            style.rect.height = *height;
        }
        if (direction) {
            style.stack.direction = *direction;
        }
        if (fontSize) {
            style.fontSize = *fontSize;
        }
    }

    size_t UIVariants::Apply(UIElement& root, eastl::span<const UIVariant> variants, const Rect& bounds) {
        ResetActive(root);
        size_t matched = 0;
        for (const UIVariant& variant : variants) {
            if (!variant.when.Matches(bounds)) {
                continue;
            }
            ++matched;
            for (const UIElementOverride& override : variant.overrides) {
                ApplyOverride(root, override);
            }
        }
        return matched;
    }

}  // namespace BECore
