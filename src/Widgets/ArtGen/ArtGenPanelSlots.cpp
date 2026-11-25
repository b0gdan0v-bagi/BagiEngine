#include "ArtGenPanelSlots.h"

#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/ArtGen/ArtGenPanelModel.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Reference/ArtGenReferencePanel.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr StringView kWorkflowInputsSlotName = "artgenWorkflowInputs";
        constexpr StringView kReferencePictureSlotName = "artgenReferencePicture";
        constexpr StringView kVariantsSlotName = "artgenVariants";
        constexpr float kThumbnailSize = 160.0f;
        constexpr const char* kDiceTip = "Use this seed for the next Generate (with another subject, a close picture)";
        constexpr size_t kChoiceTextSize = 160;
        constexpr size_t kInputTextSize = 512;
        constexpr const char* kNumberFormat = "%.3f";

    }  // namespace

    void ArtGenPanelSlots::Register() {
        PanelSlots& slots = PanelSlots::GetInstance();
        _workflowInputs = slots.Register(kWorkflowInputsSlotName, [this] { DrawWorkflowInputs(); });
        _referencePicture = slots.Register(kReferencePictureSlotName, [this] { DrawReferencePicture(); });
        _variants = slots.Register(kVariantsSlotName, [this] { DrawVariants(); });
    }

    void ArtGenPanelSlots::DrawReferencePicture() {
        _model._referencePanel.DrawPicture(_model.GetSession());
    }

    void ArtGenPanelSlots::DrawVariants() {
        ArtGenSession& session = _model.GetSession();
        const ArtGenRun& run = session.GetRun();
        if (run.variants.empty() || !run.setName.empty()) {
            return;
        }
        const auto& palette = GetImGuiPalette();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (kThumbnailSize + spacing)));
        const Optional<int64_t>& selected = _model.GetSelectedSeed();
        ImGui::BeginChild("##variants");
        for (size_t i = 0; i < run.variants.size(); ++i) {
            if (i % static_cast<size_t>(columns) != 0) {
                ImGui::SameLine();
            }
            const ArtGenVariant& variant = run.variants[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##thumb", ImVec2(kThumbnailSize, kThumbnailSize))) {
                _model.NotePick(variant.seed);
            }
            const ITexture* texture = i < _model._thumbnails.size() ? _model._thumbnails[i].Get() : nullptr;
            const bool loaded = !variant.IsAudio() && texture != nullptr && texture->GetState() == ResourceState::Loaded;
            if (loaded) {
                DrawArtGenVariantDragSource([&] { return MakeArtGenDragPayload(ArtGenDragKind::Image, variant.seed, {}, {}, SuggestedArtGenSpriteName({}, StringView(run.subject))); }, texture);
            }
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 corner(origin.x + kThumbnailSize, origin.y + kThumbnailSize);
            if (variant.IsAudio()) {
                drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
                const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_AUDIO_LINES);
                drawList->AddText(ImVec2(origin.x + (kThumbnailSize - iconSize.x) * 0.5f, origin.y + (kThumbnailSize - iconSize.y) * 0.5f), ImGui::GetColorU32(palette.accent), ICON_LC_AUDIO_LINES);
            } else if (loaded) {
                const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
                DrawArtGenChecker(drawList, origin, kThumbnailSize, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg));
                drawList->AddImage(id, origin, corner);
            } else {
                drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
            }
            if (selected == variant.seed) {
                drawList->AddRect(origin, corner, ImGui::GetColorU32(palette.accent), 0.0f, 0, kSelectedBorder);
            }
            if (loaded && i < _model._verdicts.size()) {
                DrawArtGenVerdictMark(drawList, origin, kThumbnailSize, _model._verdicts[i]);
            }
            if (IconButton(ICON_LC_DICES, kDiceTip, "##seed")) {
                _model.NoteDice(variant.seed);
            }
            if (selected == variant.seed && !variant.IsAudio()) {
                ImGui::SameLine();
                if (IconButton(ICON_LC_IMAGE_PLUS, kArtGenUseAsReferenceTip, "##reference")) {
                    _model.NoteUseAsReference(variant.seed);
                }
            }
            ImGui::SameLine();
            {
                ScopedMonoFont mono;
                ImGui::TextDisabled("%lld", static_cast<long long>(variant.seed));
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::EndChild();
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
