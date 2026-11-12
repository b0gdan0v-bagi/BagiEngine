#pragma once

#include <BECore/UI/UIWebReport.h>

namespace BECore {

    /**
     * @brief The web-style front-end of the UI: HTML-like markup and CSS-like styles in, the text of a `.ui.xml` document out.
     *
     * It builds nothing itself. The result is the XML the engine's own front-end reads, so the elements come from the same factories, the screen is laid
     * out, tested and drawn by the same code, and `UIDocument` / `UICanvasComponent` do not know which front-end wrote the file. What this class owns is the
     * translation:
     *
     * | HTML | Element |
     * |------|---------|
     * | `div`, `section`, `nav`, `ul`, `li`, ... | `UIPanel` when it has a `background-image`, else `UIElement` (a group) |
     * | `span`, `p`, `h1`..`h6`, `label`, or any element that holds only text | `UIElement` with a `UITextComponent` (its `background` is the element's `color`, the fill behind the text) |
     * | `button` | `UIElement` with `UITextComponent`, `UIInputClicker` and `UITooltipComponent` (`title`); also as a segment of a segmented group |
     * | `button` with `data-icon` | the same, the icon in a child `<N>Icon` (and `<N>Caption` with text); `-be-icon-color` is its `tint` (`ui-controls.md`) |
     * | `img` | `UIElement` with a `UIImageComponent` (`src` is the sprite name) |
     * | any element with `data-widget="segmented"` | `UISegmented` (`data-bind` is `bind`; its `<button data-value>` children are the segments, composed like any button) |
     * | any element with `data-widget="toggle"` | a row `UIElement` with a clicker and `UIToggleComponent`, children `<N>Caption` and `<N>Switch` (`ui-controls.md`) |
     * | any element with `data-widget="slider"` | `UIElement` with `UIInputClicker` (`stateLook="false"`) and `UISliderComponent` |
     * | any element with `data-widget="progress"` | `UIProgressBar` (`data-bind`, `data-min`, `data-max`) |
     * | `id` | `name` |
     *
     * Styles come from `<style>`, `<link rel="stylesheet" href>` (read through the loader function) and `style=""`, in the order of the document, and the cascade
     * is the CSS one: specificity (ids, classes, tags), then source order, `!important` first and a `style=""` over a rule. `color`, `font-size`, `font-family`, `font-weight`,
     * `text-shadow`, `-be-text-outline`, `text-align` and `line-height` are inherited. `@media` blocks become responsive variants: for each block the cascade is run again with its rules
     * and what changed becomes the overrides of the variant, so a variant can change only what UIElementOverride can (visibility, anchor, offset, size, stack
     * direction, font size). The engine-only `-be-track-color`, `-be-fill-color` and `-be-thumb-color` set `trackColor`, `fillColor` and `thumbColor` of the three widgets, `-be-icon-color` the
     * `tint` of the icon of a button (a toggle's `trackColor` and `thumbColor` that the author left out are written from its text colour, as the migration step does, because the switch cannot see the
     * caption); the states of the widgets are derived, so a state rule on them is reported. `:hover`, `:active`, `:disabled` and `:checked` on a button set its four fills
     * (`hoverColor`, `pressedColor`, `disabledColor`, `selectedColor`).
     *
     * The box (`UIBoxStyle`): `border-radius` and its four longhands (px, one value per corner), `border` / `border-width` / `border-color` / `border-style` (`solid`, `none`; a border is
     * drawn inside the rect and is not layout), `linear-gradient()` of two colors in `background` or `background-image` (it replaces `background-color`; a `url()` beside it is the sprite
     * drawn over it), `box-shadow` of one layer (the shadow) or two (the first is the glow, the second the shadow) and the engine-only `-be-highlight` (the inner top line) and `-be-shape: round`
     * (`partShape` of a toggle, slider or progress bar). The shifts of a button's states follow its fill by themselves, so a state rule changes the background only (the far end of a gradient
     * is derived from its start); a variant cannot change the box.
     *
     * Everything outside the subset is an issue in the UIWebReport and is left out; the supported properties are listed in `.claude/rules/ui-web.md`.
     * The conversion is pure: no resource, window or renderer is touched, so it is tested with strings.
     */
    class UIWebCompiler {
    public:
        /// Gives the text of the style sheet at @p href (relative to the document), or nothing when it cannot be read
        using StylesheetLoader = eastl::function<std::optional<eastl::string>(eastl::string_view href)>;

        /// @p loadStylesheet may be empty when the document has no `<link>`
        [[nodiscard]] static eastl::string Compile(eastl::string_view html, const StylesheetLoader& loadStylesheet, UIWebReport& report);
    };

}  // namespace BECore
