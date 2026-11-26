#pragma once

#include <BECore/Scene/IComponent.h>
#include <BECore/Utils/MoveOnlyFunction.h>

namespace BECore {

    class ComponentDefaults;
    struct ComponentTouches;

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
     * With @p touches the fields a client of the control API changed are marked, and an edit removes the mark: the generic inspector marks each field where
     * it stands, a custom one gets a line per mark at its top and an edit of anything removes them all. With no mark and no flash nothing differs.
     *
     * With @p defaults the fields that differ from the default are marked and have a reset arrow (DrawDefaultMark): the generic inspector beside each field,
     * a custom one in a line per field at its top. A click resets that field once the inspector has run and removes its touch mark; the answer is true then.
     * A field at its default draws nothing more.
     *
     * @param component The component to inspect and edit (must not be null)
     * @param touches What TouchHighlights knows of this component; null draws it unmarked
     * @param defaults What the component is compared with and reset by; null marks no difference
     * @return True if any field was changed
     */
    bool RenderComponentInspector(IComponent* component, const ComponentTouches* touches = nullptr, ComponentDefaults* defaults = nullptr);

}  // namespace BECore
