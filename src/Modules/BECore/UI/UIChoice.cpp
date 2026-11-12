#include "UIChoice.h"

#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITextComponent.h>

namespace BECore::UIChoice {

    namespace {

        const eastl::string& EmptyText() {
            static const eastl::string none;
            return none;
        }

        /// The text component of the first descendant of @p element that has one; a subtree whose root has a clicker is another button's and is not entered
        const UITextComponent* FindCaption(const UIElement& element) {
            for (const auto& child : element.children) {
                if (child->GetComponent<UIInputClicker>()) {
                    continue;
                }
                if (const UITextComponent* caption = child->GetComponent<UITextComponent>()) {
                    return caption;
                }
                if (const UITextComponent* deeper = FindCaption(*child)) {
                    return deeper;
                }
            }
            return nullptr;
        }

    }  // namespace

    bool IsOption(const UIElement& element) {
        return element.Is<UIButton>() || element.GetComponent<UIInputClicker>() != nullptr;
    }

    PoolString GetValue(const UIElement& element) {
        if (const auto* button = element.Cast<UIButton>()) {
            return button->value;
        }
        const UIInputClicker* clicker = element.GetComponent<UIInputClicker>();
        return clicker ? clicker->value : PoolString{};
    }

    const eastl::string& GetText(const UIElement& element) {
        if (const auto* button = element.Cast<UIButton>()) {
            return button->text;
        }
        if (const UITextComponent* own = element.GetComponent<UITextComponent>()) {
            return own->text;
        }
        const UITextComponent* caption = FindCaption(element);
        return caption ? caption->text : EmptyText();
    }

}  // namespace BECore::UIChoice
