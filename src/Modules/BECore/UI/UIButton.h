#pragma once

#include <BECore/UI/UILabel.h>

namespace BECore {

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
     */
    class UIButton : public UILabel {
        BE_CLASS(UIButton)
    public:
        UIButton();
        ~UIButton() override = default;

        BE_REFLECT_FIELD PoolString action;
        BE_REFLECT_FIELD UIColor hoverColor;
        BE_REFLECT_FIELD UIColor pressedColor;
        BE_REFLECT_FIELD UIColor disabledColor;
        BE_REFLECT_FIELD bool disabled = false;  ///< not `enabled`: a vector of factory-made objects writes an `enabled` attribute of its own

        void DrawSelf(IRenderer& renderer) override;
        void OnClick() override;

        /// The fill for the state the button is in now
        [[nodiscard]] Color GetCurrentFill() const;
    };

}  // namespace BECore
