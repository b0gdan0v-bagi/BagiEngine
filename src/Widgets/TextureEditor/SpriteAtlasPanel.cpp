#include "SpriteAtlasPanel.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kNameFieldWidth = 200.0f;
        constexpr float kNumberFieldWidth = 90.0f;
        constexpr int kPaddingStep = 1;
        constexpr int kPaddingStepFast = 4;

        PoolString GroupOf(const SpriteEntry& entry) {
            return entry.atlas.has_value() ? entry.atlas->name : PoolString{};
        }

        /// The sprites of the active scene that show these library entries show the new pixels from the next frame
        void ReloadInActiveScene(const Vector<PoolString>& sprites) {
            Scene* scene = CoreManager::GetSceneManager().GetActiveScene();
            if (scene == nullptr) {
                return;
            }
            for (const PoolString sprite : sprites) {
                ReloadSpritesUsing(scene->GetRootNode(), sprite, PoolString{});
            }
        }

    }  // namespace

    bool SpriteAtlasPanel::Render(TextureLibrary& library, const SpriteEntry& entry) {
        static_assert(kNameSize == kMaxSpriteNameLength + 1, "the name field holds the longest atlas name the model accepts");
        if (!ImGui::CollapsingHeader(ICON_LC_PACKAGE " Atlas")) {
            return false;
        }

        const PoolString group = GroupOf(entry);
        const bool packed = IsPackedSprite(entry);
        SyncNameField(library, entry, group);

        ImGui::TextUnformatted("Atlas:");
        ImGui::SameLine();
        ImGui::BeginDisabled(packed);
        ImGui::SetNextItemWidth(kNameFieldWidth);
        ImGui::InputText("##atlas_name", _name, sizeof(_name));
        const bool nameLeft = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        ItemTooltip("The atlas this sprite belongs to; sprites with the same name are packed into assets/atlases/<name>.png. Empty: none");
        if (packed) {
            ImGui::TextDisabled("Packed on '%s': Unpack to change", group.CStr());
        } else if (nameLeft && ApplyGroupName(library, entry)) {
            return true;
        }

        {
            ImGui::TextUnformatted("Padding");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(kNumberFieldWidth);
            const ScopedMonoFont mono;
            ImGui::InputInt("##atlas_padding", &_settings.padding, kPaddingStep, kPaddingStepFast);
            _settings.padding = eastl::clamp(_settings.padding, 0, kMaxAtlasPadding);
        }
        ItemTooltip("Empty pixels between two sprites, so a filtered edge does not take in a neighbour");
        ImGui::SameLine();
        if (ImGui::RadioButton("Power of two##atlas_mode", _settings.sizeMode == AtlasSizeMode::PowerOfTwo)) {
            _settings.sizeMode = AtlasSizeMode::PowerOfTwo;
        }
        ItemTooltip("Both sides of the atlas round up to a power of two");
        ImGui::SameLine();
        if (ImGui::RadioButton("Tight##atlas_mode", _settings.sizeMode == AtlasSizeMode::Tight)) {
            _settings.sizeMode = AtlasSizeMode::Tight;
        }
        ItemTooltip("The atlas is the bounding box of the sprites");

        const AtlasCounts counts = group.Empty() ? AtlasCounts{} : AtlasSummary(library, group.ToStringView());
        if (group.Empty()) {
            ImGui::TextDisabled("No atlas: name one to group this sprite with others");
        } else {
            ImGui::Text("Atlas '%s': %u sprites, %u packed", group.CStr(), counts.members, counts.packed);
        }

        bool changed = false;
        ImGui::BeginDisabled(counts.members == 0);
        if (IconTextButton(ICON_LC_PACKAGE_PLUS, "Pack", "Compose the group into assets/atlases/<name>.png from the sprites' sources; Save writes the library")) {
            changed = Pack(library, group);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(counts.packed == 0);
        if (IconTextButton(ICON_LC_PACKAGE_OPEN, "Unpack", "Point the packed sprites back at their own files; the atlas file stays on disk")) {
            changed |= Unpack(library, group);
        }
        ImGui::EndDisabled();

        if (_status[0] != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text, _hasError ? GetImGuiPalette().error : GetImGuiPalette().text);
            ImGui::TextWrapped("%s", _status);
            ImGui::PopStyleColor();
        }
        return changed;
    }

    void SpriteAtlasPanel::SyncNameField(const TextureLibrary& library, const SpriteEntry& entry, PoolString group) {
        if (entry.name == _boundSprite && group == _boundGroup) {
            return;
        }
        if (entry.name != _boundSprite) {
            _status[0] = '\0';
            _hasError = false;
        }
        // A repack from here must make the file the group was packed as (and the file Accept makes), so the fields follow the group;
        // a group nobody packed yet keeps what the person set
        if (group != _boundGroup && !group.Empty() && HasPackRecord(library, group.ToStringView())) {
            _settings = PackSettingsOf(library, group.ToStringView());
        }
        _boundSprite = entry.name;
        _boundGroup = group;
        snprintf(_name, sizeof(_name), "%s", group.CStr());
    }

    bool SpriteAtlasPanel::ApplyGroupName(TextureLibrary& library, const SpriteEntry& entry) {
        auto result = SetSpriteAtlas(library, entry.name, StringView(_name));
        if (!result) {
            SetStatus(StringView(result.error()), true);
            return false;
        }
        SetStatus({}, false);
        return *result;
    }

    bool SpriteAtlasPanel::Pack(TextureLibrary& library, PoolString group) {
        FileSystem& files = CoreManager::GetFileSystem();
        auto resolve = [&files](StringView path) { return files.ResolvePath(path); };
        const AtlasIo io{files.GetMountedPath(MountPoints::assets), resolve};

        auto written = WriteAtlas(library, group.ToStringView(), _settings, io);
        if (!written) {
            SetStatus(StringView(written.error()), true);
            return false;
        }

        // The file under this path is new: a texture loaded from the old one is stale
        CoreManager::GetResourceManager().Evict(written->texturePath.ToStringView());
        ReloadInActiveScene(written->changed);
        const String text = Format("Packed {} sprites into {} ({}x{})", written->changed.size(), written->texturePath.ToStringView(), written->width, written->height);
        SetStatus(StringView(text), false);
        return true;
    }

    bool SpriteAtlasPanel::Unpack(TextureLibrary& library, PoolString group) {
        const FileSystem& files = CoreManager::GetFileSystem();
        auto exists = [&files](PoolString path) { return files.Exists(path.ToStringView()); };

        auto unpacked = UnpackAtlas(library, group.ToStringView(), exists);
        if (!unpacked) {
            SetStatus(StringView(unpacked.error()), true);
            return false;
        }

        for (const PoolString texture : unpacked->oldTextures) {
            CoreManager::GetResourceManager().Evict(texture.ToStringView());
        }
        ReloadInActiveScene(unpacked->changed);
        const String text = Format("Unpacked {} sprites; {} stays on disk", unpacked->changed.size(), AtlasTexturePath(group.ToStringView()).ToStringView());
        SetStatus(StringView(text), false);
        return true;
    }

    void SpriteAtlasPanel::SetStatus(StringView text, bool isError) {
        _hasError = isError;
        if (text.empty()) {
            _status[0] = '\0';
            return;
        }
        snprintf(_status, sizeof(_status), "%.*s", static_cast<int>(text.size()), text.data());
    }

}  // namespace BECore
