#pragma once

#include "ConfigFieldSchema.h"

namespace BECore {

    /**
     * @brief Result returned by field inspector rendering functions
     */
    struct InspectorResult {
        bool changed = false;
        eastl::string newValue;
    };

    /**
     * @brief Render a type-appropriate ImGui control for the given field hint
     *
     * Dispatches to the correct renderer based on hint.type:
     * - Bool   -> Checkbox
     * - Int    -> InputInt
     * - Float  -> InputFloat
     * - Enum   -> Combo dropdown
     * - Flags  -> Checkbox per flag, joined with '|'
     * - String -> InputText (confirmed on Enter)
     *
     * @param widgetId Unique per-attribute string used as ImGui widget ID (## prefix applied internally)
     * @param currentValue Current attribute value to display
     * @param hint Field type hint with optional enum/flag options
     * @return InspectorResult with changed=true and newValue when the user commits a change
     */
    InspectorResult RenderFieldInspector(const eastl::string& widgetId, eastl::string_view currentValue, const FieldHint& hint);

    /**
     * @brief Clear persistent InputText buffers used by the String inspector
     *
     * Call when switching configs or reloading to ensure stale buffers are not displayed.
     */
    void ClearFieldInspectorState();

}  // namespace BECore
