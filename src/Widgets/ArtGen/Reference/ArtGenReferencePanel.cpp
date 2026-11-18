#include "ArtGenReferencePanel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
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
        const ArtGenReferenceModeRow& row = ArtGenReferenceModeOf(StringView(slot->mode));
        if (ImGui::BeginCombo("Mode##reference", slot->mode.c_str())) {
            for (const ArtGenReferenceModeRow& option : kArtGenReferenceModes) {
                std::snprintf(label, sizeof(label), "%.*s", static_cast<int>(option.mode.size()), option.mode.data());
                if (ImGui::Selectable(label, StringView(slot->mode) == option.mode) && StringView(slot->mode) != option.mode) {
                    ArtGenReference changed = *slot;
                    changed.mode = String(option.mode);
                    changed.weight.reset();  // the default of the new mode
                    session.SetReferenceSlot(std::move(changed));
                }
                ItemTooltip(option.tip);
            }
            ImGui::EndCombo();
        }
        ItemTooltip(row.tip);
        float weight = static_cast<float>(slot->weight ? *slot->weight : row.defaultWeight);
        if (ImGui::SliderFloat("Weight##reference", &weight, row.minWeight, row.maxWeight, "%.2f")) {
            ArtGenReference changed = *slot;
            changed.weight = static_cast<double>(weight);
            session.SetReferenceSlot(std::move(changed));
        }
        ItemTooltip(kWeightTip);
        if (IconTextButton(ICON_LC_X, "Clear", "Send no reference with the next Generate")) {
            session.ClearReferenceSlot();
        }
        ImGui::EndGroup();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kFilledNote);
        ImGui::PopTextWrapPos();
    }

}  // namespace BECore
