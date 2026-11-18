#include "SpriteAtlasPreviewPanel.h"

#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/TextureEditor/SpriteLibraryPanel.h>
#include <Widgets/TextureEditor/SpriteLibraryView.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kMaxPictureHeight = 320.0f;
        constexpr float kCheckerCell = 8.0f;
        constexpr float kNameInset = 2.0f;
        constexpr float kNameBackAlpha = 0.65f;
        constexpr float kOutlineThickness = 1.0f;
        constexpr float kHoverThickness = 2.0f;
        constexpr float kPercent = 100.0f;

        ImageSize SizeOfTexture(const ITexture* texture) {
            if (texture == nullptr) {
                return {};
            }
            return {static_cast<uint32_t>(texture->GetWidth()), static_cast<uint32_t>(texture->GetHeight())};
        }

        bool SameSettings(const PackSettings& a, const PackSettings& b) {
            return a.padding == b.padding && a.sizeMode == b.sizeMode && a.maxSide == b.maxSide;
        }

        bool SameSize(const ImageSize& a, const ImageSize& b) {
            return a.width == b.width && a.height == b.height;
        }

    }  // namespace

    PoolString SpriteAtlasPreviewPanel::Render(const TextureLibrary& library, const SpriteEntry& entry, const PackSettings& settings, SpriteTextureCache& textures) {
        if (!ImGui::CollapsingHeader(ICON_LC_LAYOUT_GRID " Atlas preview")) {
            return {};
        }
        const PoolString group = entry.atlas.has_value() ? entry.atlas->name : PoolString{};
        if (group.Empty()) {
            ImGui::TextDisabled("No atlas: name a group in the Atlas section to preview it");
            return {};
        }
        BindGroup(group);

        // Without a packed member there is no current atlas to show, so the plan is what is left
        const bool anyPacked = AtlasSummary(library, group.ToStringView()).packed > 0;
        AtlasPreviewMode mode = anyPacked ? _mode : AtlasPreviewMode::Plan;
        ImGui::BeginDisabled(!anyPacked);
        if (ImGui::RadioButton("Current##atlas_preview_mode", mode == AtlasPreviewMode::Current)) {
            _mode = AtlasPreviewMode::Current;
            mode = _mode;
        }
        ImGui::EndDisabled();
        ItemTooltip("The packed sprites as the library has them, on the atlas file");
        ImGui::SameLine();
        if (ImGui::RadioButton("Pack plan##atlas_preview_mode", mode == AtlasPreviewMode::Plan)) {
            _mode = AtlasPreviewMode::Plan;
            mode = _mode;
        }
        ItemTooltip("What Pack would make with the settings above, from the sprites' sources; nothing is written");

        EnsureBuilt(library, mode, settings, textures);

        if (_hasError) {
            ImGui::PushStyleColor(ImGuiCol_Text, GetImGuiPalette().error);
            ImGui::TextWrapped("%s", _error);
            ImGui::PopStyleColor();
        }

        const int clicked = DrawPicture(textures);

        {
            const ScopedMonoFont mono;
            ImGui::Text("%u x %u", _preview.width, _preview.height);
            ImGui::SameLine();
            ImGui::Text("fill %.1f%%", static_cast<double>(_preview.fillRatio * kPercent));
        }
        if (_preview.hidden > 0) {
            ImGui::TextDisabled("%u sprites of the group are not on this file", _preview.hidden);
        }
        if (_preview.outside > 0) {
            ImGui::TextColored(GetImGuiPalette().warning, "%u sprites reach outside the picture", _preview.outside);
        }

        return clicked >= 0 ? _preview.rects[static_cast<size_t>(clicked)].name : PoolString{};
    }

    void SpriteAtlasPreviewPanel::BindGroup(PoolString group) {
        if (group == _group) {
            return;
        }
        _group = group;
        _atlasPath = AtlasTexturePath(group.ToStringView());
    }

    void SpriteAtlasPreviewPanel::EnsureBuilt(const TextureLibrary& library, AtlasPreviewMode mode, const PackSettings& settings, SpriteTextureCache& textures) {
        const uint64_t key = AtlasPreviewKey(library, _group.ToStringView());
        const bool settingsMoved = mode == AtlasPreviewMode::Plan && !SameSettings(settings, _builtSettings);
        if (_built && key == _builtKey && _group == _builtGroup && mode == _builtMode && !settingsMoved && !TexturesMoved(textures)) {
            return;
        }
        _builtKey = key;
        _builtGroup = _group;
        _builtMode = mode;
        _builtSettings = settings;
        _built = true;
        Rebuild(library, mode, textures);
    }

    bool SpriteAtlasPreviewPanel::TexturesMoved(SpriteTextureCache& textures) const {
        for (const SeenTexture& seen : _seen) {
            if (!SameSize(SizeOfTexture(textures.Get(seen.path)), seen.size)) {
                return true;
            }
        }
        return false;
    }

    void SpriteAtlasPreviewPanel::Rebuild(const TextureLibrary& library, AtlasPreviewMode mode, SpriteTextureCache& textures) {
        _seen.clear();
        _hasError = false;
        _error[0] = '\0';
        auto sizeOf = [this, &textures](PoolString path) {
            const ImageSize size = SizeOfTexture(textures.Get(path));
            _seen.push_back(SeenTexture{path, size});
            return size;
        };

        if (mode == AtlasPreviewMode::Current) {
            const ImageSize size = sizeOf(_atlasPath);
            _preview = PreviewPackedAtlas(library, _group.ToStringView(), static_cast<float>(size.width), static_cast<float>(size.height));
            if (size.width == 0 || size.height == 0) {
                _hasError = true;
                snprintf(_error, sizeof(_error), "The atlas file %s is not loaded", _atlasPath.CStr());
            }
            return;
        }

        auto lookup = [&sizeOf](PoolString path) -> std::expected<ImageSize, String> {
            const ImageSize size = sizeOf(path);
            if (size.width == 0 || size.height == 0) {
                return std::unexpected(String("the texture is not loaded"));
            }
            return size;
        };
        auto plan = PreviewAtlasPlan(library, _group.ToStringView(), _builtSettings, lookup);
        if (!plan) {
            _preview = AtlasPreview{};
            _hasError = true;
            snprintf(_error, sizeof(_error), "%s", plan.error().c_str());
            return;
        }
        _preview = eastl::move(*plan);
    }

    int SpriteAtlasPreviewPanel::DrawPicture(SpriteTextureCache& textures) {
        const FitSize fit = FitInside(static_cast<float>(_preview.width), static_cast<float>(_preview.height), ImGui::GetContentRegionAvail().x, kMaxPictureHeight, false);
        if (fit.w <= 0.0f || fit.h <= 0.0f) {
            return -1;
        }

        const ImGuiPalette& palette = GetImGuiPalette();
        // An item with an id, so a press on the picture is seen and never starts a window move
        ImGui::InvisibleButton("##atlas_preview_picture", ImVec2(fit.w, fit.h));
        const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 corner(origin.x + fit.w, origin.y + fit.h);
        const float scale = fit.w / static_cast<float>(_preview.width);

        int hit = -1;
        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            hit = HitTestPreview(_preview, (mouse.x - origin.x) / scale, (mouse.y - origin.y) / scale);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawCheckerRect(drawList, origin, corner, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg), kCheckerCell);
        // A rect that leaves the picture is cut at its edge
        drawList->PushClipRect(origin, corner, true);
        ImVec4 backing = palette.panelBg;
        backing.w = kNameBackAlpha;
        const ImU32 backingColor = ImGui::GetColorU32(backing);
        for (const AtlasPreviewRect& item : _preview.rects) {
            const ImVec2 min(origin.x + item.rect.x * scale, origin.y + item.rect.y * scale);
            const ImVec2 max(min.x + item.rect.w * scale, min.y + item.rect.h * scale);

            const ITexture* texture = textures.Get(item.imagePath);
            if (texture != nullptr) {
                const ThumbnailUv uv = ComputeThumbnailUv(item.imageRect, texture->GetWidth(), texture->GetHeight());
                const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
                drawList->AddImage(id, min, max, ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1));
            } else {
                drawList->AddRectFilled(min, max, ImGui::GetColorU32(palette.accentMuted));
            }
            drawList->AddRect(min, max, ImGui::GetColorU32(palette.border), 0.0f, 0, kOutlineThickness);

            const ImVec2 textSize = ImGui::CalcTextSize(item.name.CStr());
            if (textSize.x + 2.0f * kNameInset <= max.x - min.x && textSize.y + 2.0f * kNameInset <= max.y - min.y) {
                const ImVec2 textAt(min.x + kNameInset, min.y + kNameInset);
                drawList->AddRectFilled(textAt, ImVec2(textAt.x + textSize.x, textAt.y + textSize.y), backingColor);
                drawList->AddText(textAt, ImGui::GetColorU32(palette.text), item.name.CStr());
            }
        }
        if (hit >= 0) {
            const Rect& rect = _preview.rects[static_cast<size_t>(hit)].rect;
            const ImVec2 min(origin.x + rect.x * scale, origin.y + rect.y * scale);
            drawList->AddRect(min, ImVec2(min.x + rect.w * scale, min.y + rect.h * scale), ImGui::GetColorU32(palette.accent), 0.0f, 0, kHoverThickness);
        }
        drawList->PopClipRect();

        if (hit >= 0 && ImGui::BeginTooltip()) {
            const AtlasPreviewRect& item = _preview.rects[static_cast<size_t>(hit)];
            ImGui::TextUnformatted(item.name.CStr());
            ImGui::TextColored(palette.textDisabled, "Rect %g, %g, %g x %g", item.rect.x, item.rect.y, item.rect.w, item.rect.h);
            ImGui::TextColored(palette.textDisabled, "From %s", item.imagePath.CStr());
            ImGui::EndTooltip();
        }
        return pressed ? hit : -1;
    }

}  // namespace BECore
