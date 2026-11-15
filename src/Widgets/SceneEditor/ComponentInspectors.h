#pragma once

#include <BECore/Scene/IComponent.h>
#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    /**
     * @brief Inspector function type: renders ImGui widgets for a component
     *
     * Called with the raw IComponent pointer; returns true if any field changed.
     */
    using InspectorFunc = MoveOnlyFunction<bool(IComponent*)>;

    /**
     * @brief Register a custom inspector for a component type (by type hash)
     *
     * Overrides the default reflection-based fallback for the given type.
     */
    void RegisterComponentInspector(uint64_t typeHash, InspectorFunc func);

    /**
     * @brief Register a typed custom inspector for component type T
     *
     * Wraps the type-safe callback and registers it under T::GetStaticTypeMeta().typeHash. The wrapper holds a function that is
     * bigger than the default buffer, so it takes one allocation, once per registration at start-up.
     */
    template <typename T>
    void RegisterComponentInspector(MoveOnlyFunction<bool(T&)> func) {
        RegisterComponentInspector(T::GetStaticTypeMeta().typeHash, [f = std::move(func)](IComponent* comp) -> bool { return f(static_cast<T&>(*comp)); });
    }

    /**
     * @brief Render ImGui controls for editing a component's fields
     *
     * Dispatch order:
     *   1. Custom inspector registered via RegisterComponentInspector(), if any
     *   2. Generic ImGuiPropertyVisitor via component->AcceptPropertyVisitor()
     *
     * Changes are applied directly to the component (live editing).
     *
     * @param component The component to inspect and edit (must not be null)
     * @return True if any field was changed
     */
    bool RenderComponentInspector(IComponent* component);

}  // namespace BECore
