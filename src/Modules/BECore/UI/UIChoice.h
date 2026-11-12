#pragma once

#include <BECore/UI/UIElement.h>

namespace BECore {

    /**
     * @brief What a child of a choice group (`UISegmented`, `UIDropdown`) is: the one place that tells a segment or an option apart.
     *
     * An option is any element with a `UIInputClicker` (a composed button, with or without an icon). The groups ask only this. Nothing allocates: the answers are views and references into the
     * element.
     */
    namespace UIChoice {

        /// An element that has a `UIInputClicker`
        [[nodiscard]] bool IsOption(const UIElement& element);

        /// The value the option goes out with: the `value` of the clicker; empty for a non-option
        [[nodiscard]] PoolString GetValue(const UIElement& element);

        /// The caption of the option: the `UITextComponent::text` of the element or of its first descendant that has one (depth first, never into a
        /// descendant that has a clicker of its own); empty when there is none. The reference stays valid as long as the element and its tree do
        [[nodiscard]] const eastl::string& GetText(const UIElement& element);

    }  // namespace UIChoice

}  // namespace BECore
