#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>

namespace BECore {

    /**
     * @brief Result returned by field inspector rendering functions
     */
    struct InspectorResult {
        bool changed = false;
        String newValue;
    };

    /**
     * @brief Render a type-appropriate ImGui control for the given field hint
     *
     * Dispatches to the correct renderer based on hint.type:
     * - Bool   -> Checkbox
     * - Int    -> InputInt
     * - Float  -> InputFloat
     * - Enum   -> Combo dropdown; a current value that is not an option is shown in the combo and marked "(not a known value)"
     * - Flags  -> Checkbox per option, joined with '|'; a token of the value that is not an option stays as a checked row marked
     *             "(not a known value)" until the user unchecks it
     * - String -> InputText (confirmed on Enter)
     *
     * @param widgetId Unique per-attribute string; it goes on the ImGui id stack around the controls, whose own labels are fixed
     * @param currentValue Current attribute value to display
     * @param hint Field type hint with optional enum/flag options
     * @return InspectorResult with changed=true and newValue when the user commits a change
     */
    InspectorResult RenderFieldInspector(StringView widgetId, StringView currentValue, const FieldHint& hint);

    /**
     * @brief Clear persistent InputText buffers used by the String inspector
     *
     * Call when switching configs or reloading to ensure stale buffers are not displayed.
     */
    void ClearFieldInspectorState();

}  // namespace BECore
