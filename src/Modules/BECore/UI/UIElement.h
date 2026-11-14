#pragma once

#include <BECore/UI/UIBoxStyle.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UIElementStyle.h>
#include <BECore/UI/UIStateBlend.h>
#include <BECore/UI/UIStateLook.h>

namespace BECore {

    class IRenderer;
    class UIThemes;

    /// How an element lays out its children: each one against the element's rect through its own anchor, or one after another
    CORE_ENUM(UILayoutKind, uint8_t, Anchor, Stack)

    /// What an element says about its content once it has a rect: the layout validator reads it
    struct UIContentFit {
        bool hasContent = false;  ///< the element shows something that has to fit (a text)
        bool fits = true;         ///< the content fits the rect
        bool canShrink = false;   ///< when it does not fit, the element cuts it with an ellipsis or shrinks it, so it is not a defect
    };

    /**
     * @brief A node of a canvas's UI tree: a rect in the UI units of its canvas and children. The controls that stay classes (panel, stack, group, dropdown, progress bar) derive from it; a text, a
     * picture, a button, a toggle and a slider are an element with components.
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
     *
     * **State.** `hovered` and `pressed` are set by the canvas, `selected` and `groupDisabled` by whoever owns a choice; `GetVisualState()` is what they add up to and `UIStateLook` (the pure
     * rules) turns it into a look. The element's own box takes that look (the fills `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`, or the derived shifts) only when a
     * component says `GivesStateLook()`; a panel that merely `blocksInput` is hovered too and must not light up. A control that is its own class (`UIDropdown`) draws the state look itself.
     * With a `stateDuration` above 0 (or `UISettings::stateTransition`) a change of state eases: the box, and the tint and the text colour of the children, go from the look shown to the new one over
     * that time (`UIBlend`, advanced by the draw only; `GetStateLook()` stays the pure look of the state now).
     * An element that does not `OwnsState()` (no `UIInputClicker`) is drawn in the state of its nearest ancestor that does (`GetVisualState`), so the `UIImageComponent` and the
     * `UITextComponent` of the children of a composed button follow its hover, press and disabled state; the child itself is not hovered (its flags, the hit test and `ui_tree` are its own).
     *
     * **Theme tokens.** A colour field may be a token (`color="@primary"`, UIThemes). The canvas calls `ResolveTheme()` before it draws the element: the token's colour is written into the
     * channels of every colour field (the element's own, the box's, its controls' and components'), so every reader of a colour keeps reading channels. `_themeVersion` is the version of
     * the table the channels were last resolved at; it is runtime state, neither reflected nor saved, and an element is never copied with it (elements are shared by pointer; a new one
     * starts at 0 and resolves on its first draw). Code that assigns a token to a live element calls `ResetTheme()`.
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
            false;                               ///< a child of the canvas root only: laid out against the whole canvas, not the safe area (a background); the validator spares it and its descendants
        BE_REFLECT_FIELD UIColor hoverColor;     ///< the fill under the pointer; transparent: `color` lightened (the state looks: UIStateLook)
        BE_REFLECT_FIELD UIColor pressedColor;   ///< the fill while pressed; transparent: `color` darkened
        BE_REFLECT_FIELD UIColor disabledColor;  ///< the fill when disabled; transparent: `color` at half alpha
        BE_REFLECT_FIELD UIColor selectedColor;  ///< the fill when selected; transparent: `color` brightened
        BE_REFLECT_FIELD float opacity = 1.0f;   ///< paint only: the element and its whole subtree are drawn at this alpha factor, 0..1 (laid out, hit and listed as ever; hide with `visible`)
        BE_REFLECT_FIELD float stateDuration =
            -1.0f;  ///< seconds the look takes to ease between states (this element's box, and its children's picture and text); -1: `UISettings::stateTransition`, 0: off
        Rect rect;  ///< laid out: canvas coordinates; empty while the element or an ancestor is hidden
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIElement>> children;      // not the IntrusivePtr alias: the generator looks for the long name to find a factory field
        BE_REFLECT_FIELD eastl::vector<IntrusivePtrNonAtomic<UIComponent>> components;  // the same: the long name; one of each type, in draw order

        /// Pointer state, set by the canvas that owns the tree: the pointer is over the element; a press that began on the element is still held
        bool hovered = false;
        bool pressed = false;
        bool selected = false;       ///< runtime, set by whoever owns the choice: drawn as the chosen one (a bar, a brighter fill)
        bool groupDisabled = false;  ///< runtime: the group this element is in is disabled
        bool inputLocked = false;    ///< runtime, set by the layout pass of the transitions: the element or an ancestor is showing / hiding with `inputDuringTransition` off; the canvas consumes what
                                     ///< lands on it and delivers nothing (`UITransitionComponent`)

        /// Draws this element, not its children, with the view transform of the canvas in effect (units of the canvas). The base draws the box (`DrawBox`) of `color` and `box`
        virtual void DrawSelf(IRenderer& renderer);

        /// Draws the components in list order, with the view transform of the canvas in effect. The canvas calls it after `DrawSelf` and before the children
        void DrawComponents(IRenderer& renderer);

        /// Resolves the theme tokens of this element (not its children: the canvas walks the tree) when the version of @p themes moved since the last call; at once otherwise. The
        /// canvas calls it before `DrawSelf`, so a theme switch recolours the open screen at the next frame with no tree edit and no `Invalidate()`; a hidden element resolves when it is shown
        void ResolveTheme(UIThemes& themes);

        /// Forgets what was resolved: the next `ResolveTheme` resolves again. **Call it after assigning a token to a live element** (`color = UIColor::FromToken(...)`); a token loaded from a file
        /// needs nothing, the element has not been resolved yet
        void ResetTheme() {
            _themeVersion = 0;
        }

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
            ResetTheme();  // its colours are not resolved yet
            return *component;
        }

        /// The pointer and the owner's flags as the state look reads them: `pressed` only while the pointer is still over the element, `disabled` is `IsDisabled()`, `checked` is what a component says
        /// (`UIComponent::IsChecked`). An element that has no `UIInputClicker` of its own but was drawn inside one that has (`SetInheritedState`) answers with that ancestor's state: the image and the
        /// text of a button's children follow it
        [[nodiscard]] UIStateLook::State GetVisualState() const;

        /// The element is the owner of the state its descendants without an owner are drawn in, and is itself drawn in its own: it has a `UIInputClicker`, or is a control that reacts to the
        /// pointer by itself (`UIDropdown` answers true, so it below an element with a clicker shows its own state, not the ancestor's)
        [[nodiscard]] virtual bool OwnsState() const;

        /// Runtime, called by the canvas before it draws the element: the state of the nearest ancestor that `OwnsState()`, null for none. Ignored by an element that `OwnsState()`. Not
        /// reflected and not saved; the element's own flags, `ui_tree` and the hit test never read it
        /// @p duration is that owner's `GetStateDuration()`: the children follow the owner's transition, not their own
        void SetInheritedState(const UIStateLook::State* state, float duration);

        /// How long the look of this element eases between states, in seconds, 0 for none: its own `stateDuration`, else `UISettings::stateTransition`; an element drawn inside an owner of the state
        /// (`SetInheritedState`) answers with the owner's, so a button's picture and caption move with its box
        [[nodiscard]] float GetStateDuration() const;

        /// The element does not react: `groupDisabled` or the `disabled` of its `UIInputClicker` (a control that has a `disabled` of its own adds it). A disabled element still takes the click and
        /// does nothing with it
        [[nodiscard]] virtual bool IsDisabled() const;

        /// The area the `UITextComponent` of this element draws and measures its text in: the rect, unless the element keeps part of it for something else. A dropdown answers the part
        /// left of its arrow. Only the text component asks; the other components (a picture, a switch, a slider) work in the rect
        [[nodiscard]] virtual Rect GetComponentArea() const {
            return rect;
        }

        /// The text a `UITextComponent` of this element shows instead of its own `text`, null for none (the default). A dropdown answers the caption of the chosen option, so the authored `text`
        /// (its placeholder) stays what a file holds. It must not allocate: the component asks on every draw and every check
        [[nodiscard]] virtual const String* GetTextOverride() const {
            return nullptr;
        }

        /// The text of the tooltip the canvas shows after a hover or a long touch press, empty for none: the `UITooltipComponent` text (a control with a tooltip of its own overrides it); it may hold
        /// `{#key}` and `{var}`
        [[nodiscard]] virtual const String& GetTooltipText() const;

        /**
         * Draws one part of a control (a track, a fill, a thumb) in @p area with @p fill. `Square` is the plain `DrawFilledRect` of the part, the call the control always made.
         * `Round` is a box whose only setting is a corner radius of half the shorter side: a pill for a track, a circle for a square thumb. A part shorter than the track is a
         * smaller pill (the fill of a progress bar at 2 % is a short capsule, not a track-high slice), which is accepted. Nothing for an empty @p area in the round shape.
         * Public because the components of a control call it on their owner (a switch's track and thumb, a slider's parts).
         */
        void DrawPart(IRenderer& renderer, const Rect& area, const Color& fill, UIPartShape shape);

        /// The box, the fill and the gradient end for the state the element is in now (pure: reads the state and changes nothing)
        [[nodiscard]] UIStateLook::Look GetStateLook() const;

        /// The colour of the selection (the bar, the border and the glow of a selected box): the text colour of a control, of its `UITextComponent` or else of the first child that has one (the
        /// caption of a composed button), white for a plain element
        [[nodiscard]] virtual Color GetStateAccent() const;

        /// Some component shows the pointer state in the box, so `DrawSelf` draws the state look
        [[nodiscard]] bool GivesStateLook() const;

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
         * Writes the current theme's colours into every colour field of this element that is a token: its own `color` and state fills, the box, and the components. A control that
         * declares colour fields overrides it, calls the base first and resolves its own (`themes.Resolve(field)`). Called only through `ResolveTheme`, which decides when; it reads the
         * theme and writes channels, nothing else (no layout, no `Invalidate()`)
         */
        virtual void ApplyTheme(UIThemes& themes);

        /// Draws the box in the look of the state the element is in now and, when it is selected, the bar along its foot. `DrawSelf` does it when a component gives the state look; a control
        /// that always shows its state calls it itself
        void DrawStateBox(IRenderer& renderer);

        /// The look to draw for @p target (this element's state look now): @p target itself when `GetStateDuration()` is 0, else what the blend of this element shows at the game time. Call it
        /// once per draw; a control that draws its own state box (`UIDropdown`) passes its look through it
        [[nodiscard]] const UIStateLook::Look& ShownLook(const UIStateLook::Look& target);

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

    private:
        bool _hasInheritedState = false;  ///< runtime: `_inheritedState` was passed down by the canvas at the last draw
        UIStateLook::State _inheritedState;
        float _inheritedDuration = 0.0f;        ///< runtime: the owner's `GetStateDuration()`, passed down with it
        UIBlend<UIStateLook::Look> _lookBlend;  ///< runtime: the box as it is shown while a state change eases; not saved, not copied with the element
        uint64_t _themeVersion = 0;             ///< UIThemes::GetVersion() at the last resolve; 0: never (the table starts at 1)

        // The triangles of DrawBox, kept so that a steady frame allocates nothing
        eastl::vector<GeometryVertex> _boxVertices;
        eastl::vector<int> _boxIndices;
    };

}  // namespace BECore
