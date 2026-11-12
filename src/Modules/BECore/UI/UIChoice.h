#pragma once

#include <BECore/UI/UIElement.h>

namespace BECore {

    /**
     * @brief What a child of a choice group (`UISegmented`, `UIDropdown`) is: the one place that tells a segment or an option apart.
     *
     * An option is a `UIButton` (old content) or any element with a `UIInputClicker` (the composed button, with or without an icon). The groups ask only this, so the two forms
     * behave the same. Nothing allocates: the answers are views and references into the element.
     */
    namespace UIChoice {

        /// A `UIButton`, or an element that has a `UIInputClicker`
        [[nodiscard]] bool IsOption(const UIElement& element);

        /// The value the option goes out with: the `value` of the `UIButton`, else of the clicker; empty for a non-option
        [[nodiscard]] PoolString GetValue(const UIElement& element);

        /// The caption of the option: the `text` of the `UIButton`, else the `UITextComponent::text` of the element or of its first descendant that has one (depth first, never into a
        /// descendant that has a clicker of its own); empty when there is none. The reference stays valid as long as the element and its tree do
        [[nodiscard]] const eastl::string& GetText(const UIElement& element);

    }  // namespace UIChoice

}  // namespace BECore
