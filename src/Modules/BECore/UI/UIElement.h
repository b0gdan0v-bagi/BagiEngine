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

        /// The left button went down on this element (it is `interactive`): the canvas calls it at once, not on the release. @p x and @p y are the point in the units of the canvas, like
        /// `rect`. A click is still decided on the release (`OnClick`); this is for a control that acts while the press is held, such as a slider
        virtual void OnPress(float x, float y) {
            static_cast<void>(x);
            static_cast<void>(y);
        }

        /// The pointer moved while the left button is held and the press began on this element: called on every motion, wherever the pointer is (also outside the element), until the
        /// release. Not called for a hover, after the release, or for a press the canvas did not take. @p x and @p y as for `OnPress`
        virtual void OnDrag(float x, float y) {
            static_cast<void>(x);
            static_cast<void>(y);
        }

        /// **Popup protocol.** An element whose children are a popup (a dropdown's list) says so with `HasPopupChildren()`: the canvas takes those children out of the flow. Closed, their
        /// rects are empty (not drawn, not hit, not validated, not in `ui_tree` as visible). Open, `LayoutPopup` places them against the content bounds of the canvas, they are drawn
        /// after the whole tree, hit-tested before it, and a left press anywhere else closes the popup (`ClosePopup`) and is consumed. The defaults are no popup, so no other control changes.
        /// The open state is runtime UI state: it is not reflected and not saved
        [[nodiscard]] virtual bool HasPopupChildren() const {
            return false;
        }

        /// The popup is open: only asked when `HasPopupChildren()`
        [[nodiscard]] virtual bool IsPopupOpen() const {
            return false;
        }

        /// Sets `rect` of the children (the layout then lays out their own children) while the popup is open. @p contentBounds is the safe area of the canvas, in canvas units;
        /// this element's own `rect` is already laid out. A child that is not `active.visible` gets an empty rect afterwards
        virtual void LayoutPopup(const Rect& contentBounds) {
            static_cast<void>(contentBounds);
        }

        /// The canvas closes the popup: a press landed outside it, or the canvas can no longer take input, or another popup was opened
        virtual void ClosePopup() {}

        /// A click ended on @p child, a direct child of this popup, or on something inside it: called instead of the child's `OnClick`. The element decides what follows, also
        /// whether the popup closes
        virtual void OnPopupChoice(UIElement& child) {
            static_cast<void>(child);
        }

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
