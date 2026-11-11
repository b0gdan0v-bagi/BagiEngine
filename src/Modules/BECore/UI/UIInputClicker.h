#pragma once

#include <BECore/UI/UIComponent.h>

namespace BECore {

    /**
     * @brief Makes its element a button: a click on it emits `InputEvents::ActionEvent(action, value)`. `<component type="UIInputClicker" action="Play" value="easy"/>`.
     *
     * The element is then `IsInteractive()` (hit-testable, its touch size is checked, the validator and the preview see it as a target) and its own box shows the state look
     * (`GivesStateLook()`): the fills `hoverColor`, `pressedColor`, `disabledColor`, `selectedColor` of the element, or the derived shifts. The clicker reads the flags the canvas
     * already sets (`hovered`, `pressed`) and draws nothing, so the image and the text beside it follow the state by their own `hoverTint` / `hoverTextColor` ...
     *
     * `disabled` is this component's flag; `groupDisabled` and `selected` are the element's (set by whoever owns a choice). A disabled element still takes the press and the release
     * (what is below does not get the click) and emits nothing, like a disabled `UIButton`. The target is the rect of the element, never of its image.
     */
    class UIInputClicker : public UIComponent {
        BE_CLASS(UIInputClicker)
    public:
        UIInputClicker() = default;
        ~UIInputClicker() override = default;

        BE_REFLECT_FIELD PoolString action;
        BE_REFLECT_FIELD PoolString value;       ///< emitted with the action
        BE_REFLECT_FIELD bool disabled = false;  ///< not `enabled`: a vector of factory-made objects writes an `enabled` attribute of its own

        void OnClick(UIElement& owner) override;
        [[nodiscard]] bool MakesInteractive() const override;
        [[nodiscard]] bool GivesStateLook() const override;
    };

}  // namespace BECore
