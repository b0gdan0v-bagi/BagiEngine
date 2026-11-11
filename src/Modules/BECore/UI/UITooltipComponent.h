#pragma once

#include <BECore/UI/UIComponent.h>

namespace BECore {

    /**
     * @brief The tooltip of its element: `<component type="UITooltipComponent" text="{#tip.settings}"/>`.
     *
     * The canvas shows `text` after a hover or a long touch press on the element (`UIElement::GetTooltipText`, the rules are `UITooltip`), so an icon-only button can say what it does.
     * It needs no clicker, but the canvas asks only hit-testable elements (a `UIInputClicker` or `blocksInput`). `text` may hold `{#key}` and `{var}` like the text of a label. The component only
     * holds the text.
     */
    class UITooltipComponent : public UIComponent {
        BE_CLASS(UITooltipComponent)
    public:
        UITooltipComponent() = default;
        ~UITooltipComponent() override = default;

        BE_REFLECT_FIELD eastl::string text;  ///< empty: no tooltip
    };

}  // namespace BECore
