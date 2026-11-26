#include "NumberField.h"

#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ScopedFloatingWidget.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr size_t kNameLabelSize = 128;
        /// A longer name is cut in the item's label, so the label always ends with the id that tells two items apart
        constexpr size_t kNameInLabel = 80;

        /// How much of an axis colour the frame of its field takes: at rest, under the pointer, held
        constexpr float kAxisTint = 0.35f;
        constexpr float kAxisTintHovered = 0.45f;
        constexpr float kAxisTintActive = 0.55f;
        /// The frame colours a tint pushes
        constexpr int kTintedColours = 3;

        /// The press the pointer drags a name from. One for all fields: ImGui has one active item, and each press takes it over
        struct NameDrag {
            ImGuiID owner = 0;
            double atPress = 0.0;
        };

        NameDrag& GetNameDrag() {
            static NameDrag drag;
            return drag;
        }

        ImVec4 Tinted(const ImVec4& frame, const ImVec4& axis, float share) {
            return ImVec4(ImLerp(frame.x, axis.x, share), ImLerp(frame.y, axis.y, share), ImLerp(frame.z, axis.z, share), frame.w);
        }

    }  // namespace

    ScopedAxisTint::ScopedAxisTint(StringView name) {
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImVec4* colour = nullptr;
        switch (FieldPresentation::AxisOf(name)) {
            case FieldPresentation::Axis::None:
                return;
            case FieldPresentation::Axis::X:
                colour = &palette.axisX;
                break;
            case FieldPresentation::Axis::Y:
                colour = &palette.axisY;
                break;
            case FieldPresentation::Axis::Z:
                colour = &palette.axisZ;
                break;
        }
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Tinted(palette.frameBg, *colour, kAxisTint));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Tinted(palette.frameBg, *colour, kAxisTintHovered));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Tinted(palette.frameBg, *colour, kAxisTintActive));
        _pushed = true;
    }

    ScopedAxisTint::~ScopedAxisTint() {
        if (_pushed) {
            ImGui::PopStyleColor(kTintedColours);
        }
    }

    Optional<double> DrawNumberName(StringView name, const char* id, const FieldPresentation::Presentation& presentation, double value) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(name.data(), name.data() + name.size());
        // A map key from a file may be empty, and ImGui reads a zero width as "all that is left"
        if (name.empty()) {
            return {};
        }

        const float padding = ImGui::GetStyle().FramePadding.y;
        const ImVec2 corner{ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y - padding};
        const ImVec2 size{ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y + 2.0f * padding};
        char label[kNameLabelSize];
        snprintf(label, sizeof(label), "%.*s##label_%s", static_cast<int>(ImMin(name.size(), kNameInLabel)), name.data(), id);
        {
            const ScopedFloatingWidget floating;
            ImGui::SetCursorScreenPos(corner);
            ImGui::InvisibleButton(label, size);
        }

        NameDrag& drag = GetNameDrag();
        if (ImGui::IsItemActivated()) {
            drag = {ImGui::GetItemID(), value};
        }
        const bool held = ImGui::IsItemActive();
        if (held || ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        // A click that never left its place is no edit, whatever the constraints would make of the value
        if (!held || drag.owner != ImGui::GetItemID() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            return {};
        }
        return FieldPresentation::Dragged(presentation, drag.atPress, ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x);
    }

}  // namespace BECore
