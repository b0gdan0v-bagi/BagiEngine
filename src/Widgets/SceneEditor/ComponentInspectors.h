#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    /**
     * @brief Render ImGui controls for editing a component's fields
     *
     * Dispatches to the correct renderer based on component type.
     * Changes are applied directly to the component (live editing).
     *
     * @param component The component to inspect and edit
     * @return True if any field was changed
     */
    bool RenderComponentInspector(IntrusivePtr<IComponent> component);

}  // namespace BECore
