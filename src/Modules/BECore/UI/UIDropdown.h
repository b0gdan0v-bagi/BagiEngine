#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIStateLook.h>
#include <EASTL/array.h>

namespace BECore {

    /**
     * @brief A choice of many values behind one row: the header shows the chosen option and an arrow, a click opens a list of the options above everything of the canvas.
     *
     * `<element type="UIDropdown" name="Level" bind="level" action="SetLevel" x="20" y="20" width="240" height="56" optionHeight="56">` with a `<components>` of one
     * `<component type="UITextComponent" text="Pick one" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/>` (the header text) and `<children>` of an option per child:
     * `<element type="UIElement" value="easy" ...>` with a `UITextComponent` and a `UIInputClicker` (the composed button; an icon is a child) (`UIChoice`: an option is an element with a
     * `UIInputClicker`). `bind` names a `UIVars` variable, like `UISegmented`: the option (a direct child) whose `value` equals the variable is the chosen one. The header text is that option's
     * caption (`UIChoice::GetText`: any `{#key}` or `{name}` in it is expanded as in the option itself, so a language switch rewords the header); with no option chosen it is the `text` of the
     * component, a placeholder or empty. The font, the colour and the size (`fontSize` of the element) are the component's; it draws in `GetComponentArea()` and shows `GetTextOverride()`, the two
     * hooks of `UIElement` that this class answers. Only the options are children of interest, anything else is ignored. An option has no `action` (the dropdown emits it): the clicker of a composed
     * option carries `value` only.
     *
     * **The dropdown owns no choice.** A click on an option emits `ActionEvent(action, value)` with the option's `value` and changes nothing else; the game answers by setting the
     * variable (game -> `UIVars` -> widget). That the list is open is runtime UI state (like `hovered`), neither reflected nor saved.
     *
     * The list is a popup (`UIElement::HasPopupChildren`): closed, the options have no rect (not drawn, not hit, not validated, not visible to `ui_tree`); open, `GetPopupGeometry` puts
     * them in a column under the header, at least as wide as it, flipped above the header when they do not fit below, otherwise shifted into the safe area of the canvas. There is no
     * scrolling: a list taller than the canvas is reported by the validator. **Every option needs a height**: its own authored `height`, else `optionHeight`, else the height of the
     * header (an option the same size as the header is the usual case, and 48 units at least keeps it a touch target). A click on the header toggles the list (`disabled` never opens
     * it); a `disabled` option takes the click and does nothing, the list stays open; a press anywhere else closes the list and is consumed (`UICanvasComponent`).
     *
     * The header is the touch target (`interactive`). It is the box of any element (`box`: rounded corners, border, gradient, shadow; with the default style one rect of `color`), its
     * fill and gradient end with the hover, press and disabled shifts of a button (`GetCurrentBox`); the options have a box of their own (an option is drawn by the canvas in its own state, the
     * children of a composed option in the state of the option). The text is laid out left of the arrow, and `CheckContent()` measures it there. `arrowColor` is derived from the `textColor` of the
     * text component when left transparent; the arrow points down closed and up open, so the state does not rest on color alone.
     */
    class UIDropdown : public UIElement {
        BE_CLASS(UIDropdown)
    public:
        UIDropdown();
        ~UIDropdown() override = default;

        BE_REFLECT_FIELD PoolString bind;    ///< the variable that holds the `value` of the chosen option
        BE_REFLECT_FIELD PoolString action;  ///< emitted on a choice, with the `value` of the option
        BE_REFLECT_FIELD bool disabled = false;
        BE_REFLECT_FIELD UIColor arrowColor;         ///< the arrow; transparent is the `textColor` of the `UITextComponent`, else white
        BE_REFLECT_FIELD float optionHeight = 0.0f;  ///< the height of an option that has none of its own; 0 is the height of the header

        static constexpr float kTextInset = 12.0f;    ///< the air left of the header text
        static constexpr float kArrowMargin = 14.0f;  ///< the air right of the arrow
        static constexpr float kArrowGap = 8.0f;      ///< the air between the text and the arrow
        static constexpr float kArrowWidth = 16.0f;   ///< the arrow's width in canvas units
        static constexpr float kArrowAspect = 0.5f;   ///< its height, as a share of its width
        static constexpr int kArrowRows = 6;          ///< the arrow is this many strips of `DrawFilledRect`

        /// Where the parts of the header are: the area of the text and the box of the arrow. Pure geometry, shared by the draw and `CheckContent()`
        struct HeaderParts {
            Rect text;
            Rect arrow;
        };
        [[nodiscard]] static HeaderParts GetHeaderParts(const Rect& header);

        /// The strips of the arrow in @p box: the widest at the top for a down arrow, at the bottom for an up arrow
        [[nodiscard]] static Array<Rect, kArrowRows> GetArrowStrips(const Rect& box, bool pointsUp);

        /// The size one option asks for in the list
        struct OptionSize {
            float width = 0.0f;  ///< 0 asks for nothing: the column is as wide as the header
            float height = 0.0f;
        };

        /// Where the list is: one rect per option in order, and the column that holds them
        struct PopupGeometry {
            Vector<Rect> options;
            Rect list;
            bool above = false;  ///< the list sits over the header, not under it
        };

        /// The pure rules of the list. A column as wide as the widest of the header and the options (never wider than @p bounds), directly under @p header, left edges aligned; when it does not
        /// fit below and fits above it goes above the header; when it fits on neither side it is shifted to stay inside @p bounds (a list taller than @p bounds starts at its top edge)
        [[nodiscard]] static PopupGeometry GetPopupGeometry(const Rect& header, const Vector<OptionSize>& options, const Rect& bounds);

        [[nodiscard]] bool IsOpen() const {
            return _open;
        }
        /// Opens the list; a `disabled` dropdown stays closed
        void Open();
        void Close();
        void Toggle();

        /// Sets `selected` and `groupDisabled` of the options from the variable and `disabled`
        void SyncChildren();

        /// The value of the bound variable; empty when `bind` is empty or the variable is not set
        [[nodiscard]] StringView GetSelectedValue() const;

        /// Whether a click on @p option emits `ActionEvent(action, option value)`: not when the dropdown or the option is disabled (`IsDisabled()`), or `action` is empty
        [[nodiscard]] bool EmitsOnChoice(const UIElement& option) const;

        /// The option whose `value` equals the variable; null when there is none
        [[nodiscard]] UIElement* FindChosenOption() const;

        /// The header fill for the state now: `color`, shifted by hover and press, half alpha when `disabled`
        [[nodiscard]] Color GetCurrentFill() const;

        /// The box, the fill and the gradient end of the header for the state now (pure): the fill and the gradient end shifted as `GetCurrentFill` does; `disabled` drops the shadow and
        /// the glow and fades the border and the highlight; hover and press leave them as authored (the dropdown has no state colors of its own)
        [[nodiscard]] UIStateLook::Look GetCurrentBox() const;

        /// The arrow color: `arrowColor`, else the `textColor` of the text component, else white; half alpha when `disabled`
        [[nodiscard]] Color GetCurrentArrow() const;

        /// The box, then the arrow. The header text is the `UITextComponent`, which the canvas draws right after this, in the area left of the arrow (the two never overlap)
        void DrawSelf(IRenderer& renderer) override;
        void OnClick() override;

        /// `disabled` or the group's: the header answers for itself, whatever it sits in
        [[nodiscard]] bool IsDisabled() const override {
            return disabled || groupDisabled;
        }
        /// The header has its own state (hover, press), not that of an ancestor with a clicker
        [[nodiscard]] bool OwnsState() const override {
            return true;
        }

        [[nodiscard]] bool HasPopupChildren() const override {
            return true;
        }
        [[nodiscard]] bool IsPopupOpen() const override {
            return _open && !disabled;
        }
        void LayoutPopup(const Rect& contentBounds) override;
        void ClosePopup() override {
            Close();
        }
        void OnPopupChoice(UIElement& child) override;

        /// The header text against the area left of the arrow, not the whole rect
        [[nodiscard]] UIContentFit CheckContent(float width, float height, float scale) const override;

        /// The text area of the header: left of the arrow (`GetHeaderParts`)
        [[nodiscard]] Rect GetComponentArea() const override;
        /// The caption of the chosen option (`UIChoice::GetText`), null when none is chosen: the header then shows the placeholder, the component's own `text`
        [[nodiscard]] const String* GetTextOverride() const override;

    protected:
        /// Resolves the colour fields of this control (UIElement::ApplyTheme): `arrowColor`, after the element's
        void ApplyTheme(UIThemes& themes) override;

    private:
        bool _open = false;  // runtime UI state, not reflected
    };

}  // namespace BECore
