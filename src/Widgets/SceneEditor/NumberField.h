#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/SceneEditor/FieldPresentation.h>
#include <Widgets/SceneEditor/FieldTooltips.h>
#include <imgui.h>

namespace BECore {

    /// While it lives, the frames of the fields have the colour of the axis @p name names (`FieldPresentation::AxisOf`); another name changes nothing
    class ScopedAxisTint {
    public:
        explicit ScopedAxisTint(StringView name);
        ~ScopedAxisTint();
        ScopedAxisTint(const ScopedAxisTint&) = delete;
        ScopedAxisTint& operator=(const ScopedAxisTint&) = delete;

    private:
        bool _pushed = false;
    };

    /**
     * @brief The name of a number, on the line of the field drawn last; a press on it and a horizontal drag change the number
     *
     * The name is an item of its own, `<name>##label_<id>`, over the text, and takes no room beyond it. In a disabled scope it does not drag.
     *
     * @param value The number as it is now: what a drag that starts in this frame counts from
     * @return The value the drag asks for (`FieldPresentation::Dragged`); empty while the pointer drags nothing
     */
    Optional<double> DrawNumberName(StringView name, const char* id, const FieldPresentation::Presentation& presentation, double value);

    /**
     * @brief One number of the inspector: the field in the mono font under the hidden label `##<id>`, then its name (DrawNumberName)
     *
     * ImGui draws the label of a drag field with the font of the field, so the name is drawn apart and stays in the UI font. The field is a slider
     * or a drag as @p presentation says, an edit of either goes through `FieldPresentation::Constrain`, and the tooltip shows over both.
     *
     * @return True when the user changed @p value
     */
    template <typename T>
    bool DrawNumber(StringView name, const char* id, const FieldPresentation::Presentation& presentation, T& value, ImGuiDataType dataType) {
        constexpr size_t kLabelSize = 32;
        // A tooltip is a string literal of the reflection or a text of FieldTooltips, so its view ends with a null
        const char* const tooltip = presentation.tooltip.empty() ? nullptr : presentation.tooltip.data();
        ImGui::BeginDisabled(presentation.disabled);
        char label[kLabelSize];
        snprintf(label, sizeof(label), "##%s", id);
        bool changed = false;
        {
            const ScopedAxisTint tint(name);
            const ScopedMonoFont mono;
            if (presentation.widget == FieldPresentation::NumberWidget::Slider) {
                const T low = FieldPresentation::FromDouble<T>(presentation.rangeMin);
                const T high = FieldPresentation::FromDouble<T>(presentation.rangeMax);
                changed = ImGui::SliderScalar(label, dataType, &value, &low, &high);
            } else {
                changed = ImGui::DragScalar(label, dataType, &value, static_cast<float>(presentation.speed));
            }
        }
        WrappedItemTooltip(tooltip);
        if (changed) {
            value = FieldPresentation::FromDouble<T>(FieldPresentation::Constrain(presentation, static_cast<double>(value)));
        }
        if (const Optional<double> dragged = DrawNumberName(name, id, presentation, static_cast<double>(value))) {
            const T next = FieldPresentation::FromDouble<T>(*dragged);
            changed |= next != value;
            value = next;
        }
        WrappedItemTooltip(tooltip);
        ImGui::EndDisabled();
        return changed;
    }

}  // namespace BECore
