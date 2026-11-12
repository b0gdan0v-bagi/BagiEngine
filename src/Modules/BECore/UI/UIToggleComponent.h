#pragma once

#include <BECore/UI/UIComponent.h>

namespace BECore {

    /**
     * @brief The behaviour of a toggle: a click asks for the opposite of what a variable says. It sits on the **row** of a toggle, the element that is hit and has the clicker.
     *
     * ```
     * <element type="UIElement" name="Sound" x="10" y="10" width="300" height="56" layout="Stack" direction="Horizontal" spacing="8" crossAlign="Center">
     *   <components>
     *     <component type="UIInputClicker"/>
     *     <component type="UIToggleComponent" bind="soundOn" action="SetSound"/>
     *   </components>
     *   <children>
     *     <element type="UIElement" name="SoundCaption" flex="1" height="100%">
     *       <components><component type="UITextComponent" text="Sound" verticalAlign="Center" wrap="false"/></components>
     *     </element>
     *     <element type="UIElement" name="SoundSwitch" height="100%" aspectRatio="1.25">
     *       <components><component type="UISwitchComponent"/></components>
     *     </element>
     *   </children>
     * </element>
     * ```
     *
     * The row is the touch target, caption and switch together. Its children draw from the row's state (`UIElement::GetVisualState`): the caption in the colours of the state, the switch
     * with its thumb at the on end when `IsChecked()`. The clicker is there for `disabled` (one source of it), for the hover and press of the row and for the state look of its plate; its
     * own `action` stays empty, because this component decides what a click emits.
     *
     * `bind` names a `UIVars` variable; the toggle is *on* when the variable equals `onValue` (default `"true"`) and off otherwise: any other value, an unset variable, an empty `bind` and an
     * empty `onValue` too. `offValue` (default `"false"`) is the value a click asks for when the toggle is on. **The toggle owns no state**: a click emits
     * `ActionEvent(action, value)` with the state the person asks for and changes nothing here; the game answers by setting the variable (game -> `UIVars` -> widget). A click with an empty
     * `action` or on a disabled row does nothing. Without a clicker beside it the toggle still clicks, but cannot be disabled and its switch does not know it is on.
     */
    class UIToggleComponent : public UIComponent {
        BE_CLASS(UIToggleComponent)
    public:
        UIToggleComponent();
        ~UIToggleComponent() override = default;

        BE_REFLECT_FIELD PoolString bind;      ///< the variable that says whether the toggle is on
        BE_REFLECT_FIELD PoolString action;    ///< emitted on a click, with the value to set
        BE_REFLECT_FIELD PoolString onValue;   ///< the value of `bind` that means on; `"true"` unless a file says otherwise
        BE_REFLECT_FIELD PoolString offValue;  ///< the value a click on an on toggle asks for; `"false"` unless a file says otherwise

        /// The variable equals `onValue`
        [[nodiscard]] bool IsOn() const;

        void OnClick(UIElement& owner) override;
        [[nodiscard]] bool MakesInteractive() const override;
        /// Whether the toggle is on: the row's state `checked`, which its switch reads
        [[nodiscard]] bool IsChecked(const UIElement& owner) const override;
    };

}  // namespace BECore
