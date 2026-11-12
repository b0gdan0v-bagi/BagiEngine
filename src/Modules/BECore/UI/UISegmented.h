#pragma once

#include <BECore/UI/UIStack.h>

namespace BECore {

    /**
     * @brief A choice group: a stack of segments of which one is the chosen one (`<element type="UISegmented" bind="side" direction="Horizontal">`).
     *
     * `bind` names a `UIVars` variable. A segment is a direct child that is a `UIButton` or an element with a `UIInputClicker` (`UIChoice::IsOption`: the composed button, with or without an
     * icon; the two forms may be mixed). The segment whose `value` equals the variable is `selected`, the others are not; an unset variable, an empty `bind` or an empty `value`
     * select nothing. `disabled` greys the whole group (`groupDisabled` of every segment). Two segments are a toggle, four are a level.
     *
     * **The group owns no choice.** A click on a segment emits `ActionEvent(action, value)` and changes nothing here: the game answers by setting the variable, the
     * same direction as a label (game -> `UIVars` -> widget), so the screen can never disagree with the game. The state is applied on the draw, before the canvas draws the
     * segments; `SyncChildren()` does it without one. Only direct children count, a nested group manages its own.
     */
    class UISegmented : public UIStack {
        BE_CLASS(UISegmented)
    public:
        UISegmented() = default;
        ~UISegmented() override = default;

        BE_REFLECT_FIELD PoolString bind;        ///< the variable that holds the `value` of the chosen segment
        BE_REFLECT_FIELD bool disabled = false;  ///< the whole group takes no clicks and is drawn grey

        void DrawSelf(IRenderer& renderer) override;

        /// Sets `selected` and `groupDisabled` of the segments from the variable and `disabled`
        void SyncChildren();

        /// The value of the bound variable, the one a segment must have to be selected; empty when `bind` is empty or the variable is not set
        [[nodiscard]] eastl::string_view GetSelectedValue() const;
    };

}  // namespace BECore
