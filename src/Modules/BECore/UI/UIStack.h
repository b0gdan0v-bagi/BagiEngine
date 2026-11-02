#pragma once

#include <BECore/UI/UIElement.h>

namespace BECore {

    /**
     * @brief A container that puts its children one after another (`<element type="UIStack" direction="Horizontal" spacing="8">`).
     *
     * A UIElement whose layout is Stack from the start. The stack properties (`direction`, `spacing`, `padding`, `wrap`, `crossAlign`) are the style attributes
     * every element has (UIElementStyle); the class is there so that a screen says what it means by its type name.
     */
    class UIStack : public UIElement {
        BE_CLASS(UIStack)
    public:
        UIStack() {
            layout = UILayoutKind::Stack;
        }
        ~UIStack() override = default;
    };

}  // namespace BECore
