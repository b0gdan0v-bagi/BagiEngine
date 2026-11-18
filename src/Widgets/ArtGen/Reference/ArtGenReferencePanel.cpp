#include "ArtGenReferencePanel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <cstdio>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kPictureSize = 72.0f;
        constexpr size_t kLabelSize = 64;
        constexpr const char* kSectionLabel = "Reference (optional)";
        constexpr const char* kWeightTip = "How strongly the picture counts: higher keeps more of it (init) or of its style (style)";
        constexpr const char* kEmptyNote = "No reference. Use as reference on a variant or an accepted sprite fills it; a preset's own reference applies while it is empty.";
        constexpr const char* kFilledNote = "Sent with the next single Generate; it replaces the preset's own reference.";
        constexpr size_t kPresetPathSize = 512;
        constexpr const char* kPresetNote = "The reference saved with this preset. A Generate or Queue with a filled slot above replaces it for that run.";
        constexpr const char* kUnreadableNote = "The preset's reference is not readable (needs a mode and an image, a weight a number); it is kept as it is until it is replaced or removed.";

        /// The mode combo of the slot and of a preset's own reference; the row picked when it is not the current one
        const ArtGenReferenceModeRow* DrawModeCombo(const char* label, const String& current) {
            const ArtGenReferenceModeRow* picked = nullptr;
            if (ImGui::BeginCombo(label, current.c_str())) {
                for (const ArtGenReferenceModeRow& option : kArtGenReferenceModes) {
                    char text[kLabelSize];
                    std::snprintf(text, sizeof(text), "%.*s", static_cast<int>(option.mode.size()), option.mode.data());
                    if (ImGui::Selectable(text, StringView(current) == option.mode) && StringView(current) != option.mode) {
                        picked = &option;
                    }
                    ItemTooltip(option.tip);
                }
                ImGui::EndCombo();
            }
            ItemTooltip(ArtGenReferenceModeOf(StringView(current)).tip);
            return picked;
        }

        /// The weight slider over the mode's row; an untouched one shows the row's default. True when it moved (@p weight is then set)
        bool DrawWeightSlider(const char* label, const ArtGenReferenceModeRow& row, Optional<double>& weight) {
            float value = static_cast<float>(weight ? *weight : row.defaultWeight);
            const bool changed = ImGui::SliderFloat(label, &value, row.minWeight, row.maxWeight, "%.2f");
            if (changed) {
                weight = static_cast<double>(value);
            }
            ItemTooltip(kWeightTip);
            return changed;
        }

    }  // namespace

    ArtGenReferencePanel::~ArtGenReferencePanel() {
        DropThumbnail();
    }

    void ArtGenReferencePanel::DropThumbnail() {
        if (_texture) {
            CoreManager::GetResourceManager().Evict(StringView(_path.data(), _path.size()));
        }
        _texture = {};
        _path.clear();
        _root.clear();
        _image.clear();
    }

    void ArtGenReferencePanel::SyncThumbnail(const String& gameRoot, const String& image) {
        if (_image == image && _root == gameRoot && !_path.empty()) {
            return;
        }
        DropThumbnail();
        _root = gameRoot;
        _image = image;
        _path = ArtGenReferenceFilePath(StringView(gameRoot.data(), gameRoot.size()), StringView(image.data(), image.size()));
        _texture = CoreManager::GetResourceManager().Load<ITexture>(StringView(_path.data(), _path.size()));
    }

    void ArtGenReferencePanel::DrawPresetReference(ArtGenSession& session, ArtGenPresetForm& form) {
        const auto& palette = GetImGuiPalette();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kPresetNote);
        if (form.HasUnreadableReference()) {
            ImGui::TextColored(palette.warning, "%s", kUnreadableNote);
        }
        ImGui::PopTextWrapPos();

        const Optional<ArtGenReference>& own = form.GetReference();
        if (own) {
            if (const ArtGenReferenceModeRow* picked = DrawModeCombo("Mode##presetReference", own->mode)) {
                ArtGenReference changed = *own;
                changed.mode = String(picked->mode);
                changed.weight.reset();
                form.SetReference(changed);
            }
            char path[kPresetPathSize];
            std::snprintf(path, sizeof(path), "%s", own->image.c_str());
            if (ImGui::InputText("Image##presetReference", path, sizeof(path))) {
                ArtGenReference changed = *own;
                changed.image = String(path);
                form.SetReference(changed);
            }
            if (ImGui::IsItemHovered()) {
                const StringView name = ArtGenReferenceFileName(StringView(own->image.data(), own->image.size()));
                char tip[kLabelSize];
                std::snprintf(tip, sizeof(tip), "%.*s", static_cast<int>(name.size()), name.data());
                ItemTooltip(tip);
            }
            Optional<double> weight = own->weight;
            if (DrawWeightSlider("Weight##presetReference", ArtGenReferenceModeOf(StringView(own->mode)), weight)) {
                ArtGenReference changed = *own;
                changed.weight = weight;
                form.SetReference(changed);
            }
        } else if (!form.HasUnreadableReference()) {
            ImGui::TextDisabled("No reference");
        }

        const Optional<ArtGenReference>& slot = session.GetReferenceSlot();
        ImGui::BeginDisabled(!slot);
        if (IconTextButton(ICON_LC_IMAGE_PLUS, "Take the slot", "Copy the picture, mode and weight of the Reference section of the Generate tab into this preset")) {
            form.SetReference(*slot);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!own && !form.HasUnreadableReference());
        if (IconTextButton(ICON_LC_X, "Remove", "The preset's file then has no reference key")) {
            form.ClearReference();
        }
        ImGui::EndDisabled();
    }

    void ArtGenReferencePanel::Draw(ArtGenSession& session) {
        const auto& palette = GetImGuiPalette();
        ImGui::SeparatorText(kSectionLabel);
        const Optional<ArtGenReference>& slot = session.GetReferenceSlot();
        if (!slot) {
            DropThumbnail();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", kEmptyNote);
            ImGui::PopTextWrapPos();
            return;
        }
        SyncThumbnail(session.GetGameRoot(), slot->image);

        ImGui::BeginGroup();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(kPictureSize, kPictureSize));
        ItemTooltip(_path.c_str());
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 corner(origin.x + kPictureSize, origin.y + kPictureSize);
        const ITexture* texture = _texture.Get();
        if (texture != nullptr && texture->GetState() == ResourceState::Loaded) {
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
            DrawArtGenChecker(drawList, origin, kPictureSize, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg));
            drawList->AddImage(id, origin, corner);
        } else {
            drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
        }
        ImGui::EndGroup();
        ImGui::SameLine();

        ImGui::BeginGroup();
        const StringView name = ArtGenReferenceFileName(StringView(slot->image.data(), slot->image.size()));
        char label[kLabelSize];
        std::snprintf(label, sizeof(label), "%.*s", static_cast<int>(name.size()), name.data());
        ImGui::TextUnformatted(label);
        if (const ArtGenReferenceModeRow* picked = DrawModeCombo("Mode##reference", slot->mode)) {
            ArtGenReference changed = *slot;
            changed.mode = String(picked->mode);
            changed.weight.reset();  // the default of the new mode
            session.SetReferenceSlot(std::move(changed));
        }
        Optional<double> weight = slot->weight;
        if (DrawWeightSlider("Weight##reference", ArtGenReferenceModeOf(StringView(slot->mode)), weight)) {
            ArtGenReference changed = *slot;
            changed.weight = weight;
            session.SetReferenceSlot(std::move(changed));
        }
        if (IconTextButton(ICON_LC_X, "Clear", "Send no reference with the next Generate")) {
            session.ClearReferenceSlot();
        }
        ImGui::EndGroup();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kFilledNote);
        ImGui::PopTextWrapPos();
    }

}  // namespace BECore
