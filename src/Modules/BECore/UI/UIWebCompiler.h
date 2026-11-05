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
     * | `span`, `p`, `h1`..`h6`, `label`, or any element that holds only text | `UILabel` (its `background` is the fill behind the text) |
     * | `button` | `UIButton` (`data-action` or `action` is the action; `disabled` disables it) |
     * | `img` | `UIImage` (`src` is the sprite name) |
     * | any element with `data-widget="segmented"` | `UISegmented` (`data-bind` is `bind`; its `<button data-value>` children are the segments) |
     * | any element with `data-widget="toggle"` | `UIToggle` (`data-bind`, `data-action`, `data-on` / `data-off` are `onValue` / `offValue`; the text is the caption) |
     * | any element with `data-widget="slider"` | `UISlider` (`data-bind`, `data-action`, `data-min`, `data-max`, `data-step`) |
     * | any element with `data-widget="progress"` | `UIProgressBar` (`data-bind`, `data-min`, `data-max`) |
     * | `id` | `name` |
     *
     * Styles come from `<style>`, `<link rel="stylesheet" href>` (read through the loader function) and `style=""`, in the order of the document, and the cascade
     * is the CSS one: specificity (ids, classes, tags), then source order, `!important` first and a `style=""` over a rule. `color`, `font-size`, `font-family`,
     * `text-align` and `line-height` are inherited. `@media` blocks become responsive variants: for each block the cascade is run again with its rules
     * and what changed becomes the overrides of the variant, so a variant can change only what UIElementOverride can (visibility, anchor, offset, size, stack
     * direction, font size). The engine-only `-be-track-color`, `-be-fill-color` and `-be-thumb-color` set `trackColor`, `fillColor` and `thumbColor` of the three widgets; the states of the
     * widgets are derived, so a state rule on them is reported. `:hover`, `:active`, `:disabled` and `:checked` on a button set its four fills (`hoverColor`, `pressedColor`, `disabledColor`,
     * `selectedColor`).
     *
     * Everything outside the subset is an issue in the UIWebReport and is left out; the supported properties are listed in `UI/CLAUDE.md`.
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
