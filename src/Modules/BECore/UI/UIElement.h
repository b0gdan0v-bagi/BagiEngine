#pragma once

#include <BECore/UI/UIRectSpec.h>

namespace BECore {

    /// How an element lays out its children: each one against the element's rect through its own anchor, or one after another
    CORE_ENUM(UILayoutKind, uint8_t, Anchor, Stack)

    /// Everything a variant can change about an element
    struct UIElementStyle {
        bool visible = true;
        UIRectSpec rect;
        UIStackSpec stack;      ///< used when the element's layout is Stack
        float fontSize = 0.0f;  ///< in UI units; 0 leaves the element's own size. Elements without text ignore it

        [[nodiscard]] bool operator==(const UIElementStyle&) const = default;
    };

    /// What an element says about its content once it has a rect: the layout validator reads it
    struct UIContentFit {
        bool hasContent = false;  ///< the element shows something that has to fit (a text)
        bool fits = true;         ///< the content fits the rect
        bool canShrink = false;   ///< when it does not fit, the element cuts it with an ellipsis or shrinks it, so it is not a defect
    };

    /**
     * @brief A node of a canvas's UI tree: a rect in the UI units of its canvas and children. The controls (panel, label, button) derive from it.
     *
     * `style` is what was authored. `active` is what is in effect after the variants of the canvas matched (UIVariants::Apply): layout reads
     * `active`, and a canvas that goes back to a shape no variant touches gets `style` again. `rect` is the result of the layout,
     * in the units of the canvas, in canvas coordinates (not relative to the parent). Public fields, like the scene components: after changing
     * one, call UICanvasComponent::Invalidate().
     */
    class UIElement : public RefCounted {
    public:
        UIElement() = default;
        ~UIElement() override = default;

        PoolString name;
        UILayoutKind layout = UILayoutKind::Anchor;
        UIElementStyle style;
        UIElementStyle active;
        Color color{0, 0, 0, 0};      ///< the fill the canvas draws for this element; a transparent color draws nothing
        bool blocksInput = false;     ///< a hit test finds it: a click on it does not reach what is below
        bool interactive = false;     ///< it takes clicks (a button): hit-testable, and the validator checks its touch size
        bool ignoreSafeArea = false;  ///< a child of the canvas root only: laid out against the whole canvas, not the safe area (a background)
        Rect rect;                    ///< laid out: canvas coordinates; empty while the element or an ancestor is hidden
        eastl::vector<IntrusivePtr<UIElement>> children;

        /// Adds a plain child with this name and returns it
        IntrusivePtr<UIElement> AddChild(PoolString childName);

        /// The first element named @p searched, this one included, depth first; null when there is none
        [[nodiscard]] UIElement* FindByName(PoolString searched);

        [[nodiscard]] bool IsHitTestable() const {
            return blocksInput || interactive;
        }

        /// Called by the validator once the element has its rect. @p scale is output pixels per UI unit, for content that is measured in pixels (a text).
        [[nodiscard]] virtual UIContentFit CheckContent(float width, float height, float scale) const;
    };

}  // namespace BECore
