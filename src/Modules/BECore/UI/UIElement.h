#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIElementStyle.h>

namespace BECore {

    class IRenderer;

    /// How an element lays out its children: each one against the element's rect through its own anchor, or one after another
    CORE_ENUM(UILayoutKind, uint8_t, Anchor, Stack)

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
     *
     * **In a file.** An element is `<element type="UIPanel" name="Hud" ...>` and its authored state is reflected: `name`, `layout`, the style as flat
     * attributes (UIElementStyle), `color`, `blocksInput`, `interactive`, `ignoreSafeArea` and a `<children>` array of elements. The type names come
     * from AbstractFactory<UIElement>, so a control is one more derived class. `active` and `rect` are results and are not saved.
     */
    class UIElement : public RefCounted {
        BE_CLASS(UIElement, FACTORY_BASE, ELEMENT = element, REGISTER_SELF)
    public:
        UIElement() = default;
        ~UIElement() override = default;

        BE_REFLECT_FIELD PoolString name;
        BE_REFLECT_FIELD UILayoutKind layout = UILayoutKind::Anchor;
        BE_REFLECT_FIELD UIElementStyle style;
        UIElementStyle active;
        BE_REFLECT_FIELD UIColor color;                                             ///< the fill the canvas draws for this element; a transparent color draws nothing
        BE_REFLECT_FIELD bool blocksInput = false;                                  ///< a hit test finds it: a click on it does not reach what is below
        BE_REFLECT_FIELD bool interactive = false;                                  ///< it takes clicks (a button): hit-testable, and the validator checks its touch size
        BE_REFLECT_FIELD bool ignoreSafeArea = false;                               ///< a child of the canvas root only: laid out against the whole canvas, not the safe area (a background)
        Rect rect;                                                                  ///< laid out: canvas coordinates; empty while the element or an ancestor is hidden
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIElement>> children;  // not the IntrusivePtr alias: the generator looks for the long name to find a factory field

        /// Pointer state, set by the canvas that owns the tree: the pointer is over the element; a press that began on the element is still held
        bool hovered = false;
        bool pressed = false;

        /// Draws this element, not its children, with the view transform of the canvas in effect (units of the canvas). The base fills the rect with `color`
        virtual void DrawSelf(IRenderer& renderer);

        /// A press and its release both landed on this element: the canvas calls it. A button emits its action here
        virtual void OnClick() {}

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
