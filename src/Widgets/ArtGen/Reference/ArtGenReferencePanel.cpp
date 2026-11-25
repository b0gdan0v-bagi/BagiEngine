#include "ArtGenReferencePanel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <Widgets/ArtGen/Sets/ArtGenSetPanel.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/ImGuiTheme.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kPictureSize = 72.0f;

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

    void ArtGenReferencePanel::DrawLoadedPicture() {
        const auto& palette = GetImGuiPalette();
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
    }

    void ArtGenReferencePanel::DrawPicture(ArtGenSession& session) {
        const Optional<ArtGenReference>& slot = session.GetReferenceSlot();
        if (!slot) {
            DropThumbnail();
            return;
        }
        SyncThumbnail(session.GetGameRoot(), slot->image);
        DrawLoadedPicture();
    }

}  // namespace BECore
