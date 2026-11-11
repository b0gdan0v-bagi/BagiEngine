#pragma once

#include <BECore/UI/UIBoxStyle.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
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
     * attributes (UIElementStyle), `color`, the box as flat attributes too (UIBoxStyle: rounded corners, border, gradient, shadow, glow, highlight), `blocksInput`, `interactive`, `ignoreSafeArea` and
     * a `<children>` array of elements. The type names come from AbstractFactory<UIElement>, so a control is one more derived class. `active` and `rect` are results and are not saved.
     *
     * **Components.** What an element shows and does can also be a list of UIComponent (`<components><component type="..."/></components>`), one of each type: the element is
     * the set of its components, not a class of its own. `DrawSelf` draws the box, `DrawComponents` the components in list order (the canvas calls it right after `DrawSelf`), then the
     * children follow. `OnClick`, `OnPress`, `OnDrag` and `CheckContent` of this class forward to the components (a subclass that overrides them keeps its own and does not),
     * and a component that takes clicks makes the element `IsInteractive()`: use that, not the field `interactive`, to ask whether it takes the pointer.
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
        BE_REFLECT_FIELD UIColor color;             ///< the fill the canvas draws for this element; a transparent color draws nothing
        BE_REFLECT_FIELD UIBoxStyle box;            ///< the shape of the fill and its paint around it; the default is the plain rect of `color`
        BE_REFLECT_FIELD bool blocksInput = false;  ///< a hit test finds it: a click on it does not reach what is below
        BE_REFLECT_FIELD bool interactive = false;  ///< it takes clicks (a button): hit-testable, and the validator checks its touch size
        BE_REFLECT_FIELD bool ignoreSafeArea =
            false;  ///< a child of the canvas root only: laid out against the whole canvas, not the safe area (a background); the validator spares it and its descendants
        Rect rect;  ///< laid out: canvas coordinates; empty while the element or an ancestor is hidden
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIElement>> children;      // not the IntrusivePtr alias: the generator looks for the long name to find a factory field
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIComponent>> components;  // the same: the long name; one of each type, in draw order

        /// Pointer state, set by the canvas that owns the tree: the pointer is over the element; a press that began on the element is still held
        bool hovered = false;
        bool pressed = false;

        /// Draws this element, not its children, with the view transform of the canvas in effect (units of the canvas). The base draws the box (`DrawBox`) of `color` and `box`
        virtual void DrawSelf(IRenderer& renderer);

        /// Draws the components in list order, with the view transform of the canvas in effect. The canvas calls it after `DrawSelf` and before the children
        void DrawComponents(IRenderer& renderer);

        /// A press and its release both landed on this element: the canvas calls it. A button emits its action here; the base forwards to the components
        virtual void OnClick();

        /// The left button went down on this element (it `IsInteractive()`): the canvas calls it at once, not on the release. @p x and @p y are the point in the units of the canvas, like
        /// `rect`. A click is still decided on the release (`OnClick`); this is for a control that acts while the press is held, such as a slider
        virtual void OnPress(float x, float y);

        /// The pointer moved while the left button is held and the press began on this element: called on every motion, wherever the pointer is (also outside the element), until the
        /// release. Not called for a hover, after the release, or for a press the canvas did not take. @p x and @p y as for `OnPress`
        virtual void OnDrag(float x, float y);

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

        /// The first component of type T, null when there is none
        template <typename T>
        [[nodiscard]] T* GetComponent() const {
            for (const auto& component : components) {
                if (T* found = component->template Cast<T>()) {
                    return found;
                }
            }
            return nullptr;
        }

        /// The component of type T, made and appended when the element has none: an element holds one of each type
        template <typename T>
        T& AddComponent() {
            if (T* existing = GetComponent<T>()) {
                return *existing;
            }
            auto component = New<T>();
            components.push_back(component);
            return *component;
        }

        /// It takes clicks: the field `interactive` (a control) or a component that takes them. The hit test, the validator's touch rule, the preview's touch marks and the canvas press read this
        [[nodiscard]] bool IsInteractive() const;

        [[nodiscard]] bool IsHitTestable() const {
            return blocksInput || IsInteractive();
        }

        /// Called by the validator once the element has its rect. @p scale is output pixels per UI unit, for content that is measured in pixels (a text). The base is the combination of
        /// the components' answers: any content that does not fit and cannot shrink is a defect
        [[nodiscard]] virtual UIContentFit CheckContent(float width, float height, float scale) const;

        /// Runs after the element is loaded from a file: drops the second component of a type (logged), so `AddComponent` and the file agree
        void Initialize();

    protected:
        /**
         * Draws the box in `rect`: shadow, glow, fill, border, highlight, in this order, as one DrawGeometry call (the order of the triangles is the order of the paint). A
         * @p boxStyle with no effect (`UIBoxStyle::HasEffect`) is the plain `DrawFilledRect` of @p fill, the call every element made before the box existed. @p fill is the colour
         * (the start of the gradient), @p gradientEnd the far colour; a control passes its state-shifted colours (a hover, a press) and leaves @p boxStyle to its authored value.
         * Nothing for an empty rect. The shadow, glow and the feather of an edge are drawn outside `rect` (paint: not layout and not validated); the clip and the view
         * transform apply to them like to every draw.
         */
        void DrawBox(IRenderer& renderer, const UIBoxStyle& boxStyle, const Color& fill, const Color& gradientEnd);

        /// `DrawBox` for any rect: draws the box in @p area instead of `rect` (a part of a control). `DrawBox` is this call with `rect`
        void DrawBoxIn(IRenderer& renderer, const Rect& area, const UIBoxStyle& boxStyle, const Color& fill, const Color& gradientEnd);

        /**
         * Draws one part of a control (a track, a fill, a thumb) in @p area with @p fill. `Square` is the plain `DrawFilledRect` of the part, the call the control always made.
         * `Round` is a box whose only setting is a corner radius of half the shorter side: a pill for a track, a circle for a square thumb. A part shorter than the track is a
         * smaller pill (the fill of a progress bar at 2 % is a short capsule, not a track-high slice), which is accepted. Nothing for an empty @p area in the round shape.
         */
        void DrawPart(IRenderer& renderer, const Rect& area, const Color& fill, UIPartShape shape);

    private:
        // The triangles of DrawBox, kept so that a steady frame allocates nothing
        eastl::vector<GeometryVertex> _boxVertices;
        eastl::vector<int> _boxIndices;
    };

}  // namespace BECore
