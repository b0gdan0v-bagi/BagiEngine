#include "AssetBrowserSlots.h"

#include <BECore/File/Path.h>
#include <Widgets/AssetBrowser/AssetBrowserPanelModel.h>
#include <Widgets/AssetBrowser/AssetDrag.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/TextureEditor/SpriteLibraryPanel.h>
#include <cfloat>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        constexpr StringView kFolderSlotName = "assetFolders";
        constexpr StringView kGridSlotName = "assetGrid";

        constexpr const char* kRootId = "##root";
        constexpr const char* kRootLabel = ICON_LC_LIBRARY " All content";

        // Sizes in em, so a tile follows the font scale
        constexpr float kTileWidthEm = 7.0f;
        constexpr float kPictureEm = 5.0f;
        constexpr float kTilePaddingEm = 0.25f;
        constexpr float kIconEm = 2.5f;
        constexpr float kCheckerCellEm = 0.6f;
        constexpr float kHalf = 0.5f;
        /// A longer file name is cut in the ImGui label only; the tile is told apart by its path
        constexpr size_t kTileLabelSize = 256;

        const char* IconOfKind(AssetKind kind) {
            switch (kind) {
                case AssetKind::Texture:
                    return ICON_LC_IMAGE;
                case AssetKind::Font:
                    return ICON_LC_TYPE;
                case AssetKind::Sound:
                    return ICON_LC_MUSIC;
                case AssetKind::Scene:
                    return ICON_LC_CLAPPERBOARD;
                case AssetKind::Node:
                    return ICON_LC_BOX;
                case AssetKind::UiDocument:
                    return ICON_LC_LAYOUT_TEMPLATE;
                case AssetKind::Config:
                    return ICON_LC_SETTINGS;
                case AssetKind::None:
                case AssetKind::Other:
                    break;
            }
            return ICON_LC_FILE;
        }

    }  // namespace

    void AssetBrowserSlots::Register() {
        PanelSlots& slots = PanelSlots::GetInstance();
        // The closures keep `this` only: they are called inside the frame and the handles end with this object
        _folderSlot = slots.Register(kFolderSlotName, [this] { DrawFolders(); });
        _gridSlot = slots.Register(kGridSlotName, [this] { DrawGrid(); });
    }

    void AssetBrowserSlots::DrawFolders() {
        const StringView selected = _model.GetView().folder;
        constexpr ImGuiTreeNodeFlags kFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        const auto noteClick = [this](PoolString folder) {
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                _model.NoteFolderClick(folder);
            }
        };

        const ImGuiTreeNodeFlags rootFlags = kFlags | ImGuiTreeNodeFlags_DefaultOpen | (selected.empty() ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
        const bool rootOpen = ImGui::TreeNodeEx(kRootId, rootFlags, "%s", kRootLabel);
        noteClick(PoolString{});
        if (!rootOpen) {
            return;
        }

        const Span<const AssetFolder> folders = _model.GetCatalog().GetFolders();
        uint32_t openLevels = 0;  // the folders below the root whose tree node is pushed now
        for (size_t i = 0; i < folders.size(); ++i) {
            const AssetFolder& folder = folders[i];
            // A parent comes before its children, so a folder deeper than what is open sits in a closed one
            if (folder.depth > openLevels) {
                continue;
            }
            for (; openLevels > folder.depth; --openLevels) {
                ImGui::TreePop();
            }

            const bool leaf = i + 1 == folders.size() || folders[i + 1].depth <= folder.depth;
            const bool isSelected = folder.path.ToStringView() == selected;
            ImGuiTreeNodeFlags flags = kFlags;
            if (leaf) {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (folder.depth == 0) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (isSelected) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            const StringView name = PathFilename(folder.path.ToStringView());
            const char* icon = isSelected ? ICON_LC_FOLDER_OPEN : ICON_LC_FOLDER;
            const bool open = ImGui::TreeNodeEx(folder.path.CStr(), flags, "%s %.*s", icon, static_cast<int>(name.size()), name.data());
            noteClick(folder.path);
            if (open && !leaf) {
                ++openLevels;
            }
        }
        for (; openLevels > 0; --openLevels) {
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    void AssetBrowserSlots::DrawGrid() {
        if (_catalogRevision != _model.GetCatalogRevision()) {
            _catalogRevision = _model.GetCatalogRevision();
            _thumbnails.ForgetFailed();
        }

        const Span<const AssetFile> files = _model.GetCatalog().GetFiles();
        const Span<const uint32_t> shown = _model.GetShownFiles();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float em = ImGui::GetFontSize();
        // Padding above the picture, under it and under the name
        const ImVec2 tile(em * kTileWidthEm, em * (kPictureEm + kTilePaddingEm * 3.0f) + ImGui::GetTextLineHeight());
        const float fitting = (ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (tile.x + style.ItemSpacing.x);
        const size_t columns = static_cast<size_t>(eastl::max(1.0f, fitting));
        const size_t rows = (shown.size() + columns - 1) / columns;

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows), tile.y + style.ItemSpacing.y);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const size_t first = static_cast<size_t>(row) * columns;
                const size_t end = eastl::min(first + columns, static_cast<size_t>(shown.size()));
                for (size_t i = first; i < end; ++i) {
                    if (i != first) {
                        ImGui::SameLine();
                    }
                    DrawTile(files[shown[i]], tile);
                }
            }
        }
    }

    void AssetBrowserSlots::DrawTile(const AssetFile& file, const ImVec2& size) {
        const StringView name = PathFilename(file.path.ToStringView());
        const bool selected = file.path.ToStringView() == _model.GetView().selectedPath;
        char label[kTileLabelSize];
        snprintf(label, sizeof(label), "##%.*s", static_cast<int>(name.size()), name.data());

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::PushID(file.path.CStr());
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, size)) {
            _model.NoteAssetClick(file.path);
        }
        ImGui::PopID();
        // The clipper submits whole rows: a tile that does not show draws and loads nothing
        const bool visible = ImGui::IsItemVisible();
        const float em = ImGui::GetFontSize();
        const bool dragged = DrawAssetDragSource(file.kind, file.path.ToStringView(), [this, &file, name, em] {
            const float side = em * kPictureEm;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(side, side));
            DrawPicture(file, min, side);
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
        });
        if (!dragged) {
            ItemTooltip(file.path.CStr());
        }
        if (!visible) {
            return;
        }

        const float padding = em * kTilePaddingEm;
        const float side = em * kPictureEm;
        const ImVec2 pictureMin(origin.x + (size.x - side) * kHalf, origin.y + padding);
        DrawPicture(file, pictureMin, side);

        const char* nameEnd = name.data() + name.size();
        const ImVec2 nameSize = ImGui::CalcTextSize(name.data(), nameEnd);
        const ImVec2 nameMin(origin.x + eastl::max(0.0f, (size.x - nameSize.x) * kHalf), pictureMin.y + side + padding);
        const ImVec2 nameMax(origin.x + size.x, nameMin.y + ImGui::GetTextLineHeight());
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), nameMin, nameMax, nameMax.x, name.data(), nameEnd, &nameSize);
    }

    void AssetBrowserSlots::DrawPicture(const AssetFile& file, const ImVec2& min, float side) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImVec2 max(min.x + side, min.y + side);
        const ITexture* texture = file.kind == AssetKind::Texture ? _thumbnails.Get(file.path) : nullptr;
        if (texture != nullptr) {
            DrawCheckerRect(drawList, min, max, ImGui::GetColorU32(palette.panelBg), ImGui::GetColorU32(palette.frameBg), ImGui::GetFontSize() * kCheckerCellEm);
            const FitSize fit = FitInside(texture->GetWidth(), texture->GetHeight(), side, side, true);
            if (fit.w <= 0.0f || fit.h <= 0.0f) {
                return;
            }
            const ImVec2 imageMin(min.x + (side - fit.w) * kHalf, min.y + (side - fit.h) * kHalf);
            const ImVec2 imageMax(imageMin.x + fit.w, imageMin.y + fit.h);
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->GetNativeHandle()));
            drawList->AddImage(id, imageMin, imageMax);
            return;
        }

        // A texture with no picture is one that did not load
        const bool failed = file.kind == AssetKind::Texture;
        const char* icon = failed ? ICON_LC_IMAGE_OFF : IconOfKind(file.kind);
        ImFont* font = ImGui::GetFont();
        const float iconSize = ImGui::GetFontSize() * kIconEm;
        const ImVec2 extent = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        const ImVec2 at(min.x + (side - extent.x) * kHalf, min.y + (side - extent.y) * kHalf);
        drawList->AddText(font, iconSize, at, ImGui::GetColorU32(failed ? palette.warning : palette.textDisabled), icon);
    }

}  // namespace BECore
