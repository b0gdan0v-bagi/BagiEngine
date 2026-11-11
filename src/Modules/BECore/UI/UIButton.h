#pragma once

#include <BECore/UI/UILabel.h>
#include <BECore/UI/UISprite.h>

namespace BECore {

    /// Where the icon of a button sits against its caption
    CORE_ENUM(UIIconPlacement, uint8_t, Left, Above)

    /**
     * @brief A button: a label on a filled rect that reacts to the pointer and emits an action when it is clicked.
     *
     * `<element type="UIButton" name="Reset" text="Reset" action="Match3Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="48" color="40,90,60,255"/>`
     *
     * A click is a press and its release, both on the button (the canvas follows the press to its release, so dragging off the button cancels it). It emits
     * `InputEvents::ActionEvent(action)`, the same event a ClickableComponent emits, so game code does not tell them apart. The fill follows the state: `color` normally,
     * `hoverColor` under the pointer, `pressedColor` while it is held on the button, `disabledColor` when `disabled`; a color left transparent is derived from `color` (lighter,
     * darker, half transparent). A button is `interactive`, so the layout validator checks that it is big enough to touch (give it 48 units at the reference size).
     * A disabled button still takes the click, it only does nothing with it.
     *
     * `value` goes out with the action, `ActionEvent(action, value)`, so several buttons can share one action and tell a listener which one was pressed (the segments of
     * a choice group). `selected` is a runtime state, set by whoever owns the choice (not read from a file): a selected button is filled from `selectedColor` (lighter
     * than `color` when left transparent) and has a bar of `kSelectedBarHeight` units in `textColor` along its bottom edge, so the selection does not rest on color alone.
     * `groupDisabled` is the same for a whole group: it counts as `disabled`.
     *
     * The state shows in the shape too when the button has a box style (`UIElement::box`; a button with the default one is drawn exactly as above and nothing below applies). The rules
     * (hover, press, selected, disabled and the numbers) are `UIStateLook`'s, shared with every element that shows a state; `GetCurrentBox` is the button's call of them. An explicit
     * `hoverColor`, `pressedColor`, `disabledColor` or `selectedColor` (fields of `UIElement`) replaces the start colour.
     *
     * **An icon.** `icon` names a sprite of the `TextureLibrary`; a button with one draws it fitted and centred in its icon rect (proportions kept, as `UIImage` does). No text: the icon
     * is alone in the rect (an icon-only button, the rect stays the touch target, never the icon). With text: `iconPlacement` `Left` puts the icon at the left edge and the caption in the
     * rest of the width, `Above` puts it at the top and the caption below. `iconSize` is the side of the icon in canvas units, 0 derives it from the rect (`kIconOnlyShare`,
     * `kIconWithTextShare`). `GetParts` is the one place that lays this out, and `CheckContent` measures the caption in the area it leaves, so a caption the icon squeezes out is a
     * `TextDoesNotFit` for the validator. The icon takes `iconTint` (`textColor` when left transparent), brightened under the pointer, darkened while held on the button and at
     * `kDisabledAlpha` when disabled (`GetCurrentIconColor`); `selected` leaves it alone. A button with no `icon` is drawn and measured exactly as before.
     *
     * `tooltip` is the text the canvas shows after a hover (or a long touch press) so an icon-only button can say what it does; it may hold `{#key}` and `{var}` like `text`. The button
     * only holds it: showing it is the canvas's matter.
     */
    class UIButton : public UILabel {
        BE_CLASS(UIButton)
    public:
        UIButton();
        ~UIButton() override = default;

        BE_REFLECT_FIELD PoolString action;
        BE_REFLECT_FIELD PoolString value;       ///< emitted with the action
        BE_REFLECT_FIELD bool disabled = false;  ///< not `enabled`: a vector of factory-made objects writes an `enabled` attribute of its own
        BE_REFLECT_FIELD PoolString icon;        ///< a TextureLibrary sprite name; empty: no icon
        BE_REFLECT_FIELD UIColor iconTint;       ///< transparent: the icon takes `textColor`
        BE_REFLECT_FIELD UIIconPlacement iconPlacement = UIIconPlacement::Left;
        BE_REFLECT_FIELD float iconSize = 0.0f;  ///< the side of the icon in canvas units; 0: derived from the rect
        BE_REFLECT_FIELD eastl::string tooltip;  ///< shown by the canvas on hover or a long touch press; may hold `{#key}` and `{var}`; empty: none

        /// The numbers of the state looks are `UIStateLook`'s; these are the names the button's tests and users know
        static constexpr float kSelectedBarHeight = UIStateLook::kSelectedBarHeight;
        static constexpr float kPressShadowOffsetScale = UIStateLook::kPressShadowOffsetScale;
        static constexpr float kPressShadowBlurScale = UIStateLook::kPressShadowBlurScale;
        static constexpr float kSelectedBorderMin = UIStateLook::kSelectedBorderMin;
        static constexpr float kSelectedGlowBlur = UIStateLook::kSelectedGlowBlur;
        static constexpr float kSelectedGlowAlpha = UIStateLook::kSelectedGlowAlpha;

        /// The icon, as a share of the shorter side of the rect when `iconSize` is 0
        static constexpr float kIconOnlyShare = 0.6f;      ///< a button with no caption
        static constexpr float kIconWithTextShare = 0.5f;  ///< a button with a caption: of its height
        static constexpr float kIconPadding = 8.0f;        ///< the air between the edge of the rect and an icon next to a caption, in canvas units
        static constexpr float kIconGap = 6.0f;            ///< and between that icon and the caption

        void DrawSelf(IRenderer& renderer) override;
        void OnClick() override;

        /// `disabled` or the group's (`selected` and `groupDisabled` are fields of `UIElement`)
        [[nodiscard]] bool IsDisabled() const override;
        [[nodiscard]] const eastl::string& GetTooltipText() const override {
            return tooltip;
        }
        [[nodiscard]] Color GetStateAccent() const override {
            return textColor;
        }

        /// Where the icon and the caption are in a rect. Pure geometry, so a test and `CheckContent()` use the same numbers. Without an icon the text area is the whole @p area; without text
        /// the icon is centred alone. Both rects stay inside @p area, however small it is (an empty rect for what has no room)
        struct Parts {
            Rect icon;
            Rect text;
        };
        [[nodiscard]] static Parts GetParts(const Rect& area, bool hasIcon, bool hasText, UIIconPlacement placement, float iconSize);

        /// The tint of the icon in the state the button is in now (pure)
        [[nodiscard]] Color GetCurrentIconColor() const;

        /// The sprite of the icon: tests and code can give the texture directly
        [[nodiscard]] UISprite& GetIconSprite() {
            return _iconSprite;
        }

        /// The caption against the area the icon leaves, not the whole rect
        [[nodiscard]] UIContentFit CheckContent(float width, float height, float scale) const override;

        /// What the button draws its box with in the state it is in now
        using Look = UIStateLook::Look;

        /// The fill for the state the button is in now
        [[nodiscard]] Color GetCurrentFill() const;

        /// The box, the fill and the gradient end for the state the button is in now (pure: it reads the state and changes nothing)
        [[nodiscard]] Look GetCurrentBox() const;

    protected:
        /// Resolves the colour fields of this control (UIElement::ApplyTheme): `iconTint`
        void ApplyTheme(UIThemes& themes) override;

    private:
        void DrawIcon(IRenderer& renderer, const Rect& area);

        UISprite _iconSprite;
    };

}  // namespace BECore
