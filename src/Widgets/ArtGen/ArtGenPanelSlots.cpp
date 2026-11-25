#include "ArtGenPanelSlots.h"

#include <Widgets/ArtGen/ArtGenPanelModel.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kWorkflowInputsSlotName = "artgenWorkflowInputs";
        constexpr size_t kChoiceTextSize = 160;
        constexpr size_t kInputTextSize = 512;
        constexpr const char* kNumberFormat = "%.3f";

    }  // namespace

    void ArtGenPanelSlots::Register() {
        _workflowInputs = PanelSlots::GetInstance().Register(kWorkflowInputsSlotName, [this] { DrawWorkflowInputs(); });
    }

    void ArtGenPanelSlots::DrawWorkflowInputs() {
        ArtGenPresetForm& form = _model.GetForm();
        const ArtGenWorkflow* workflow = _model.GetSession().FindWorkflow(StringView(form.GetWorkflow()));
        if (workflow == nullptr) {
            return;
        }
        for (const ArtGenWorkflowInput& input : workflow->inputs) {
            if (!IsArtGenServiceInput(StringView(input.name))) {
                DrawInputField(form, input);
            }
        }
    }

    void ArtGenPanelSlots::DrawInputField(ArtGenPresetForm& form, const ArtGenWorkflowInput& input) {
        const ArtGenInputValue* override = form.FindParam(StringView(input.name));
        const ArtGenInputValue& value = override != nullptr ? *override : input.defaultValue;
        const char* label = input.name.c_str();
        ImGui::PushID(label);
        if (!input.choices.empty()) {
            char current[kChoiceTextSize];
            value.ToText(current, sizeof(current));
            if (ImGui::BeginCombo(label, current)) {
                for (const ArtGenInputValue& choice : input.choices) {
                    char text[kChoiceTextSize];
                    choice.ToText(text, sizeof(text));
                    if (ImGui::Selectable(text, choice.SameStored(value))) {
                        form.SetParam(StringView(input.name), choice);
                    }
                }
                ImGui::EndCombo();
            }
        } else if (value.kind == ArtGenInputKind::Bool) {
            bool flag = value.flag;
            if (ImGui::Checkbox(label, &flag)) {
                form.SetParam(StringView(input.name), ArtGenInputValue::FromBool(flag));
            }
        } else if (value.kind == ArtGenInputKind::Integer) {
            int64_t number = value.integer;
            ScopedMonoFont mono;
            if (ImGui::InputScalar(label, ImGuiDataType_S64, &number)) {
                form.SetParam(StringView(input.name), ArtGenInputValue::FromInteger(number));
            }
        } else if (value.kind == ArtGenInputKind::Number) {
            double number = value.number;
            ScopedMonoFont mono;
            if (ImGui::InputDouble(label, &number, 0.0, 0.0, kNumberFormat)) {
                form.SetParam(StringView(input.name), ArtGenInputValue::FromNumber(number));
            }
        } else {
            char text[kInputTextSize];
            value.ToText(text, sizeof(text));
            if (ImGui::InputText(label, text, sizeof(text))) {
                form.SetParam(StringView(input.name), ArtGenInputValue::FromText(StringView(text)));
            }
        }
        ItemTooltip(override != nullptr ? "Set by this preset" : "The workflow's own value");
        if (override != nullptr) {
            ImGui::SameLine();
            if (IconButton(ICON_LC_ROTATE_CCW, "Back to the workflow's value", "##Reset")) {
                form.ClearParam(StringView(input.name));
            }
        }
        ImGui::PopID();
    }

}  // namespace BECore
