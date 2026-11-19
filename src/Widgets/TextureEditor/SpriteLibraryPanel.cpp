#include "SpriteLibraryPanel.h"

#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr float kThumbnailSize = 40.0f;
        constexpr float kRowPadding = 4.0f;
        constexpr float kThumbnailCheckerCell = 8.0f;
        constexpr float kHalf = 0.5f;
        constexpr float kDropOutlineThickness = 2.0f;
        constexpr float kDropTintAlpha = 0.18f;

        const char* const kNewSpriteHint = "Drop for a new sprite";
        const char* const kSoundNotSpriteHint = "A sound is not a sprite";

        const char* const kAllLabel = ICON_LC_LIBRARY " All sprites";
        const char* const kNoAtlasLabel = ICON_LC_IMAGE " No atlas";

        const char* BadgeIcon(SpriteAtlasState state) {
            switch (state) {
                case SpriteAtlasState::Packed:
                    return ICON_LC_PACKAGE;
                case SpriteAtlasState::Grouped:
                    return ICON_LC_LAYERS;
                case SpriteAtlasState::Single:
                    break;
            }
            return nullptr;
        }

        const char* BadgeTooltip(SpriteAtlasState state) {
            return state == SpriteAtlasState::Packed ? "Packed into its atlas" : "In an atlas group, not packed";
        }

        /// The thumbnail of @p entry in the square at @p origin: its rect of its texture, fitted and centred, over a checker; an icon when there is no picture
        void DrawThumbnail(ImDrawList* drawList, const SpriteEntry& entry, const ITexture* texture, ImVec2 origin) {
            const ImGuiPalette& palette = GetImGuiPalette();
            const ImVec2 corner(origin.x + kThumbnailSize, origin.y + kThumbnailSize);
            if (texture == nullptr) {
                drawList->AddRectFilled(origin, corner, ImGui::GetColorU32(palette.frameBg));
                const char* icon = ICON_LC_IMAGE_OFF;
                const ImVec2 iconSize = ImGui::CalcTextSize(icon);
                const ImVec2 at(origin.x + (kThumbnailSize - iconSize.x) * kHalf, origin.y + (kThumbnailSize - iconSize.y) * kHalf);
                drawList->AddText(at, ImGui::GetColorU32(entry.texturePath.Empty() ? palette.textDisabled : palette.warning), icon);
                return;
            }

            const float textureWidth = texture->GetWidth();
            const float textureHeight = texture->GetHeight();
            const ThumbnailUv uv = ComputeThumbnailUv(entry.srcRect, textureWidth, textureHeight);
            const float sourceWidth = (uv.u1 - uv.u0) * textureWidth;
            const float sourceHeight = (uv.v1 - uv.v0) * textureHeight;
            const FitSize fit = FitInside(sourceWidth, sourceHeight, kThumbnailSize, kThumbnailSize, true);

            DrawCheckerRect(drawList, origin, corner, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg), kThumbnailCheckerCell);
            if (fit.w <= 0.0f || fit.h <= 0.0f) {
                return;
            }
            const ImVec2 imageMin(origin.x + (kThumbnailSize - fit.w) * kHalf, origin.y + (kThumbnailSize - fit.h) * kHalf);
            const ImVec2 imageMax(imageMin.x + fit.w, imageMin.y + fit.h);
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
            drawList->AddImage(id, imageMin, imageMax, ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1));
        }

        void DrawTooltip(const SpriteEntry& entry, SpriteAtlasState state) {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) || !ImGui::BeginTooltip()) {
                return;
            }
            const ImGuiPalette& palette = GetImGuiPalette();
            ImGui::TextUnformatted(entry.name.CStr());
            ImGui::TextColored(palette.textDisabled, "%s", entry.texturePath.Empty() ? "(no texture)" : entry.texturePath.CStr());
            if (entry.srcRect.IsEmpty()) {
                ImGui::TextColored(palette.textDisabled, "Whole texture");
            } else {
                ImGui::TextColored(palette.textDisabled, "Rect %g, %g, %g x %g", entry.srcRect.x, entry.srcRect.y, entry.srcRect.w, entry.srcRect.h);
            }
            if (state != SpriteAtlasState::Single) {
                ImGui::TextColored(palette.textDisabled, "Atlas '%s': %s", entry.atlas->name.CStr(), BadgeTooltip(state));
            }
            if (state == SpriteAtlasState::Packed) {
                ImGui::TextColored(palette.textDisabled, "Source %s", entry.atlas->source->texturePath.CStr());
            }
            ImGui::EndTooltip();
        }

        /// The variant dragged now, or null when the drag is something else; a peek, nothing is accepted
        const ArtGenDragPayload* PeekVariant() {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload == nullptr || !payload->IsDataType(kArtGenVariantPayload) || payload->DataSize != static_cast<int>(sizeof(ArtGenDragPayload))) {
                return nullptr;
            }
            return static_cast<const ArtGenDragPayload*>(payload->Data);
        }

        /// Inside `BeginDragDropTarget` / `...Custom`: @p hot says an image variant is over the target; the return is true when it was
        /// released here (its copy in @p dropped)
        bool TakeVariant(ArtGenDragPayload& dropped, bool& hot) {
            const ArtGenDragPayload* dragged = PeekVariant();
            if (dragged == nullptr) {
                return false;
            }
            if (dragged->kind == ArtGenDragKind::Audio) {
                ImGui::SetTooltip("%s", kSoundNotSpriteHint);
                return false;
            }
            hot = true;
            const ImGuiPayload* delivered = ImGui::AcceptDragDropPayload(kArtGenVariantPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (delivered == nullptr || delivered->DataSize != static_cast<int>(sizeof(ArtGenDragPayload))) {
                return false;
            }
            dropped = *static_cast<const ArtGenDragPayload*>(delivered->Data);
            return true;
        }

    }  // namespace

    void DrawCheckerRect(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 light, ImU32 dark, float cell) {
        drawList->AddRectFilled(min, max, light);
        if (cell <= 0.0f) {
            return;
        }
        const int columns = static_cast<int>(std::ceil((max.x - min.x) / cell));
        const int rows = static_cast<int>(std::ceil((max.y - min.y) / cell));
        for (int row = 0; row < rows; ++row) {
            const float top = min.y + static_cast<float>(row) * cell;
            const float bottom = std::min(top + cell, max.y);
            for (int column = row % 2 == 0 ? 1 : 0; column < columns; column += 2) {
                const float left = min.x + static_cast<float>(column) * cell;
                drawList->AddRectFilled(ImVec2(left, top), ImVec2(std::min(left + cell, max.x), bottom), dark);
            }
        }
    }

    Optional<SpriteLibraryDrop> SpriteLibraryPanel::TakeDrop() {
        Optional<SpriteLibraryDrop> drop = _drop;
        _drop.reset();
        return drop;
    }

    void SpriteLibraryPanel::SubmitDropArea() {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        _areaMin = ImVec2(window->InnerRect.Min.x, ImGui::GetCursorScreenPos().y);
        _areaMax = window->InnerRect.Max;
        if (!ImGui::BeginDragDropTargetCustom(ImRect(_areaMin, _areaMax), window->GetID("##sprite_drop_area"))) {
            return;
        }
        ArtGenDragPayload dropped;
        if (TakeVariant(dropped, _areaHot)) {
            _drop = SpriteLibraryDrop{-1, dropped};
        }
        ImGui::EndDragDropTarget();
    }

    void SpriteLibraryPanel::DrawDropHint() const {
        if (!_areaHot || _rowHot) {
            return;
        }
        const ImGuiPalette& palette = GetImGuiPalette();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(_areaMin, _areaMax, ImGui::GetColorU32(ImGui::GetColorU32(palette.accent), kDropTintAlpha));
        drawList->AddRect(_areaMin, _areaMax, ImGui::GetColorU32(palette.accent), 0.0f, 0, kDropOutlineThickness);
        const ImVec2 textSize = ImGui::CalcTextSize(kNewSpriteHint);
        const ImVec2 at(_areaMin.x + (_areaMax.x - _areaMin.x - textSize.x) * kHalf, _areaMin.y + (_areaMax.y - _areaMin.y - textSize.y) * kHalf);
        drawList->AddText(at, ImGui::GetColorU32(palette.text), kNewSpriteHint);
    }

    void SpriteLibraryPanel::ResetFilter() {
        _filter = AtlasFilter{};
    }

    void SpriteLibraryPanel::EnsureBuilt(const TextureLibrary& library) {
        const uint64_t key = LibraryListKey(library);
        if (_built && key == _builtKey && _filter == _builtFilter) {
            return;
        }
        _groups = ListAtlasGroups(library);
        _filter = NormalizeFilter(_groups, _filter);
        _rows = FilterSprites(library, _filter);
        _spriteCount = static_cast<uint32_t>(library.GetAll().size());
        _noAtlasCount = static_cast<uint32_t>(FilterSprites(library, AtlasFilter{AtlasFilterKind::NoAtlas, PoolString{}}).size());
        _builtKey = key;
        _builtFilter = _filter;
        _built = true;

        switch (_filter.kind) {
            case AtlasFilterKind::All:
                snprintf(_filterLabel, sizeof(_filterLabel), "%s (%u)", kAllLabel, _spriteCount);
                break;
            case AtlasFilterKind::NoAtlas:
                snprintf(_filterLabel, sizeof(_filterLabel), "%s (%u)", kNoAtlasLabel, _noAtlasCount);
                break;
            case AtlasFilterKind::Group:
                snprintf(_filterLabel, sizeof(_filterLabel), ICON_LC_LAYERS " %s (%u)", _filter.group.CStr(), static_cast<uint32_t>(_rows.size()));
                break;
        }
    }

    void SpriteLibraryPanel::DrawFilterCombo() {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (!ImGui::BeginCombo("##atlas_filter", _filterLabel)) {
            return;
        }
        char label[kLabelSize];

        snprintf(label, sizeof(label), "%s (%u)###filter_all", kAllLabel, _spriteCount);
        if (ImGui::Selectable(label, _filter.kind == AtlasFilterKind::All)) {
            _filter = AtlasFilter{};
        }
        snprintf(label, sizeof(label), "%s (%u)###filter_none", kNoAtlasLabel, _noAtlasCount);
        if (ImGui::Selectable(label, _filter.kind == AtlasFilterKind::NoAtlas)) {
            _filter = AtlasFilter{AtlasFilterKind::NoAtlas, PoolString{}};
        }
        for (const AtlasGroupInfo& group : _groups) {
            const bool selected = _filter.kind == AtlasFilterKind::Group && _filter.group == group.name;
            snprintf(label, sizeof(label), ICON_LC_LAYERS " %s (%u, %u packed)###filter_%s", group.name.CStr(), group.members, group.packed, group.name.CStr());
            if (ImGui::Selectable(label, selected)) {
                _filter = AtlasFilter{AtlasFilterKind::Group, group.name};
            }
        }
        ImGui::EndCombo();
    }

    bool SpriteLibraryPanel::DrawRow(const SpriteEntry& entry, bool selected, SpriteTextureCache& textures, int index) {
        const ImGuiPalette& palette = GetImGuiPalette();
        const SpriteAtlasState state = AtlasStateOf(entry);
        const float rowHeight = kThumbnailSize + 2.0f * kRowPadding;

        ImGui::PushID(index);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::Selectable("##row", selected, 0, ImVec2(0.0f, rowHeight));
        if (ImGui::BeginDragDropTarget()) {
            ArtGenDragPayload dropped;
            if (TakeVariant(dropped, _rowHot)) {
                _drop = SpriteLibraryDrop{index, dropped};
            }
            if (_rowHot) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(palette.accent), 0.0f, 0, kDropOutlineThickness);
            }
            ImGui::EndDragDropTarget();
        }
        DrawTooltip(entry, state);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawThumbnail(drawList, entry, textures.Get(entry.texturePath), ImVec2(origin.x + kRowPadding, origin.y + kRowPadding));

        const char* badge = BadgeIcon(state);
        const float badgeWidth = badge != nullptr ? ImGui::CalcTextSize(badge).x : 0.0f;
        const float textLeft = origin.x + kRowPadding * 2.0f + kThumbnailSize;
        const float textRight = std::max(textLeft, origin.x + width - badgeWidth - kRowPadding * 2.0f);
        const float lineHeight = ImGui::GetTextLineHeight();
        const float textTop = origin.y + (rowHeight - lineHeight * 2.0f - ImGui::GetStyle().ItemInnerSpacing.y) * kHalf;

        drawList->PushClipRect(ImVec2(textLeft, origin.y), ImVec2(textRight, origin.y + rowHeight), true);
        drawList->AddText(ImVec2(textLeft, textTop), ImGui::GetColorU32(palette.text), entry.name.CStr());
        const float secondTop = textTop + lineHeight + ImGui::GetStyle().ItemInnerSpacing.y;
        const StringView file = TextureFileName(entry.texturePath);
        const char* fileEnd = file.data() + file.size();
        drawList->AddText(ImVec2(textLeft, secondTop), ImGui::GetColorU32(palette.textDisabled), file.data(), fileEnd);
        if (state != SpriteAtlasState::Single) {
            const float groupLeft = textLeft + ImGui::CalcTextSize(file.data(), fileEnd).x + ImGui::GetStyle().ItemSpacing.x;
            drawList->AddText(ImVec2(groupLeft, secondTop), ImGui::GetColorU32(palette.accent), entry.atlas->name.CStr());
        }
        drawList->PopClipRect();

        if (badge != nullptr) {
            const ImVec2 badgeAt(origin.x + width - badgeWidth - kRowPadding, origin.y + (rowHeight - lineHeight) * kHalf);
            drawList->AddText(badgeAt, ImGui::GetColorU32(state == SpriteAtlasState::Packed ? palette.accent : palette.textDisabled), badge);
        }
        ImGui::PopID();
        return clicked;
    }

    int SpriteLibraryPanel::Render(const TextureLibrary& library, int selectedIndex, SpriteTextureCache& textures) {
        EnsureBuilt(library);
        DrawFilterCombo();
        EnsureBuilt(library);

        _areaHot = false;
        _rowHot = false;
        SubmitDropArea();

        const Vector<SpriteEntry>& entries = library.GetAll();
        if (_rows.empty()) {
            EmptyState(ICON_LC_IMAGE_OFF, entries.empty() ? "No sprites yet" : "No sprite matches the filter");
            DrawDropHint();
            return -1;
        }

        int clickedIndex = -1;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(_rows.size()), kThumbnailSize + 2.0f * kRowPadding + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int index = static_cast<int>(_rows[static_cast<size_t>(row)]);
                if (DrawRow(entries[static_cast<size_t>(index)], index == selectedIndex, textures, index)) {
                    clickedIndex = index;
                }
            }
        }
        DrawDropHint();
        return clickedIndex;
    }

}  // namespace BECore
