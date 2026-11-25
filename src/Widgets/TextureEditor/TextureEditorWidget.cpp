#include "TextureEditorWidget.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/TextureEditorWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/EditorLayout/EditorDocumentRows.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kLibraryListWidth = 300.0f;
        const char* const kAddSpritePopup = "Add sprite";
        constexpr StringView kSpriteLibraryName = "sprites";

    }  // namespace

    bool TextureEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorCommandEvent, &TextureEditorWidget::OnCommand>(this);
        _documents = CoreManager::GetEditorDocuments().Add([this](Vector<EditorDocumentState>& out) { AppendDocuments(out); });
        return true;
    }

    void TextureEditorWidget::AppendDocuments(Vector<EditorDocumentState>& out) {
        out.push_back(MakeConfigDocument(CoreManager::GetConfigManager(), EditorDocumentKind::Library, kSpriteLibraryName, _isDirty, ConfigNames::textureLibrary));
    }

    void TextureEditorWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewSpriteLibrary) {
            _open = !_open;
        }
        // The same rule as the Save button: nothing to save while there are no unsaved changes
        if (event.commandId == EditorCommandIds::kTextureSave && _isDirty) {
            SaveLibrary();
        }
    }

    void TextureEditorWidget::SaveLibrary() {
        if (CoreManager::GetTextureLibrary().Save()) {
            _isDirty = false;
        }
    }

    void TextureEditorWidget::Update() {
        if (!_open) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Sprite Library", &_open)) {
            ImGui::End();
            return;
        }

        ImGui::BeginDisabled(!_isDirty);
        if (IconTextButton(ICON_LC_SAVE, "Save", "Write the sprite library to its file")) {
            SaveLibrary();
        }
        ImGui::EndDisabled();

        if (_isDirty) {
            ImGui::SameLine();
            ImGui::TextColored(GetImGuiPalette().warning, "(unsaved changes)");
        }

        if (!_dropMessage.empty()) {
            const ImGuiPalette& palette = GetImGuiPalette();
            ImGui::PushStyleColor(ImGuiCol_Text, _dropFailed ? palette.error : palette.success);
            ImGui::TextWrapped("%s", _dropMessage.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        ImGui::BeginChild("##sprite_list", ImVec2(kLibraryListWidth, 0.0f), true);
        RenderLeftPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##sprite_edit", ImVec2(0.0f, 0.0f), true);
        RenderRightPanel();
        ImGui::EndChild();

        ImGui::End();
    }

    void TextureEditorWidget::Draw() {}

    void TextureEditorWidget::Select(int index) {
        const auto& entries = CoreManager::GetTextureLibrary().GetAll();
        const bool valid = index >= 0 && index < static_cast<int>(entries.size());
        _selectedIndex = index;
        _nameDirty = false;
        _renameRefused = PoolString{};

        const StringView name = valid ? entries[static_cast<size_t>(index)].name.ToStringView() : StringView{};
        const size_t copyLen = eastl::min(sizeof(_nameEditBuffer) - 1, name.size());
        memcpy(_nameEditBuffer, name.data(), copyLen);
        _nameEditBuffer[copyLen] = '\0';
    }

    void TextureEditorWidget::SelectByName(PoolString name) {
        const auto& entries = CoreManager::GetTextureLibrary().GetAll();
        int found = -1;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            if (entries[static_cast<size_t>(i)].name == name) {
                found = i;
                break;
            }
        }
        Select(found);
    }

    void TextureEditorWidget::RenderLeftPanel() {
        auto& lib = CoreManager::GetTextureLibrary();

        const int clicked = _libraryPanel.Render(lib, _selectedIndex, _textures);
        if (clicked >= 0) {
            Select(clicked);
        }
        if (const Optional<SpriteLibraryDrop> drop = _libraryPanel.TakeDrop()) {
            OnLibraryDrop(*drop);
        }
        DrawDropPopup();

        ImGui::Separator();
        if (IconTextButton(ICON_LC_CIRCLE_PLUS, "Add", "Add a sprite entry to the library", -1.0f)) {
            SpriteEntry newEntry;
            newEntry.name = lib.MakeUniqueName("new_sprite");
            const PoolString addedName = newEntry.name;
            lib.AddOrUpdate(eastl::move(newEntry));
            _libraryPanel.ResetFilter();
            SelectByName(addedName);
            _isDirty = true;
        }
    }

    void TextureEditorWidget::OnLibraryDrop(const SpriteLibraryDrop& drop) {
        const auto& library = CoreManager::GetTextureLibrary();
        const auto& entries = library.GetAll();
        if (drop.row < 0) {
            _pendingDrop = drop.payload;
            memcpy(_popupName, drop.payload.suggestedName, sizeof(_popupName));
            PlanPopupName();
            _openDropPopup = true;
            return;
        }
        if (drop.row >= static_cast<int>(entries.size())) {
            return;
        }
        const ArtGenDropPlan plan = PlanArtGenDrop(drop.payload, entries[static_cast<size_t>(drop.row)].name.ToStringView(), library);
        if (plan.mode == ArtGenDropMode::Refused) {
            _dropMessage = plan.reason;
            _dropFailed = true;
            return;
        }
        AcceptDrop(drop.payload, plan.sprite);
    }

    void TextureEditorWidget::AcceptDrop(const ArtGenDragPayload& payload, StringView sprite) {
        ArtGenDropResult result;
        ArtGenVariantDropEvent::Emit(payload, sprite, &result);
        _dropFailed = !result.ok;
        _dropMessage = eastl::move(result.message);
        if (result.ok) {
            _libraryPanel.ResetFilter();
            SelectByName(PoolString::Intern(sprite));
        }
    }

    void TextureEditorWidget::PlanPopupName() {
        const ArtGenDragPayload named = MakeArtGenDragPayload(_pendingDrop.kind, _pendingDrop.seed, _pendingDrop.set, _pendingDrop.job, _popupName);
        _popupPlan = PlanArtGenDrop(named, StringView{}, CoreManager::GetTextureLibrary());
    }

    void TextureEditorWidget::DrawDropPopup() {
        if (_openDropPopup) {
            ImGui::OpenPopup(kAddSpritePopup);
            _openDropPopup = false;
        }
        if (!ImGui::BeginPopupModal(kAddSpritePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        const ImGuiPalette& palette = GetImGuiPalette();
        ImGui::TextUnformatted("Name of the new sprite");
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        if (ImGui::InputText("##drop_name", _popupName, sizeof(_popupName))) {
            PlanPopupName();
        }
        const bool refused = _popupPlan.mode == ArtGenDropMode::Refused;
        if (refused) {
            ImGui::PushStyleColor(ImGuiCol_Text, palette.error);
            ImGui::TextWrapped("%s", _popupPlan.reason.c_str());
            ImGui::PopStyleColor();
        } else if (_popupPlan.sprite != _popupName) {
            ImGui::TextColored(palette.textDisabled, "Taken: it will be added as '%s'", _popupPlan.sprite.c_str());
        }

        ImGui::BeginDisabled(refused);
        if (IconTextButton(ICON_LC_CHECK, "Accept", "Accept the variant as this sprite")) {
            AcceptDrop(_pendingDrop, _popupPlan.sprite);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (IconTextButton(ICON_LC_X, "Cancel", "Drop nothing")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void TextureEditorWidget::RenderRightPanel() {
        auto& lib = CoreManager::GetTextureLibrary();
        const auto& entries = lib.GetAll();

        if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(entries.size())) {
            EmptyState(ICON_LC_IMAGE, "Select a sprite from the list");
            return;
        }

        SpriteEntry entry = entries[static_cast<size_t>(_selectedIndex)];
        // A packed sprite's texture and rect point into its atlas: editing them would break the link, so they wait for Unpack
        const bool packed = IsPackedSprite(entry);

        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##sprite_name", _nameEditBuffer, sizeof(_nameEditBuffer))) {
            _nameDirty = true;
            _renameRefused = PoolString{};
        }
        if (_nameDirty && ImGui::IsItemDeactivatedAfterEdit()) {
            const PoolString newName = PoolString::Intern(StringView(_nameEditBuffer));
            if (newName != entry.name) {
                if (lib.Rename(entry.name, newName)) {
                    entry.name = newName;
                    SelectByName(newName);
                    _isDirty = true;
                } else {
                    // Rename refuses an empty name too; only a taken one is worth a warning
                    const bool taken = !newName.Empty();
                    Select(_selectedIndex);
                    if (taken) {
                        _renameRefused = newName;
                    }
                }
            }
            _nameDirty = false;
        }
        if (!_renameRefused.Empty()) {
            ImGui::TextColored(GetImGuiPalette().warning, "A sprite named '%s' already exists", _renameRefused.CStr());
        }

        ImGui::Separator();

        if (!_assetsLoaded) {
            RefreshAssetList();
        }

        int currentTexIndex = -1;
        for (int i = 0; i < static_cast<int>(_cachedAssets.size()); ++i) {
            if (_cachedAssets[i] == entry.texturePath) {
                currentTexIndex = i;
                break;
            }
        }

        ImGui::TextUnformatted("Texture:");
        ImGui::SameLine();
        ImGui::BeginDisabled(packed);
        ImGui::SetNextItemWidth(300.0f);
        const char* previewTex = currentTexIndex >= 0 ? _cachedAssets[currentTexIndex].CStr() : "(none)";
        if (ImGui::BeginCombo("##tex_path", previewTex)) {
            if (ImGui::Selectable("(none)", currentTexIndex < 0)) {
                entry.texturePath = PoolString{};
                lib.AddOrUpdate(entry);
                _isDirty = true;
            }
            for (int i = 0; i < static_cast<int>(_cachedAssets.size()); ++i) {
                const bool sel = (currentTexIndex == i);
                if (ImGui::Selectable(_cachedAssets[i].CStr(), sel)) {
                    entry.texturePath = _cachedAssets[i];
                    lib.AddOrUpdate(entry);
                    _isDirty = true;
                }
                if (sel) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (IconButton(ICON_LC_REFRESH_CW, "Rescan the assets folder for textures", "tex_refresh")) {
            _assetsLoaded = false;
            _textures.ForgetFailed();
        }
        if (packed) {
            ImGui::TextDisabled("Packed into an atlas: Unpack (Atlas section) to change the texture or the rect");
        }

        ImGui::Separator();

        // Pixels of the sheet when its texture is loaded, else 0: the slice section shows only for a loaded texture
        int sheetWidth = 0;
        int sheetHeight = 0;
        if (!entry.texturePath.Empty()) {
            // A file rewritten on disk is evicted from the cache (the Generate panel's Accept and switch do): the handle held here is the old picture
            const bool evicted = IsEvictedHandle(CoreManager::GetResourceManager().GetCache(), _previewLoadedPath, _previewTexture.Get());
            if (entry.texturePath != _previewLoadedPath || evicted) {
                _previewTexture = CoreManager::GetResourceManager().Load<ITexture>(entry.texturePath.ToStringView());
                _previewLoadedPath = entry.texturePath;
                _isDragging = false;
            }

            if (_previewTexture && _previewTexture->GetState() == ResourceState::Loaded) {
                const float texW = _previewTexture->GetWidth();
                const float texH = _previewTexture->GetHeight();
                const float maxDisplaySize = 400.0f;

                float displayW = texW;
                float displayH = texH;
                if (displayW > maxDisplaySize || displayH > maxDisplaySize) {
                    const float aspectRatio = texW / texH;
                    if (texW > texH) {
                        displayW = maxDisplaySize;
                        displayH = maxDisplaySize / aspectRatio;
                    } else {
                        displayH = maxDisplaySize;
                        displayW = maxDisplaySize * aspectRatio;
                    }
                }

                sheetWidth = static_cast<int>(texW);
                sheetHeight = static_cast<int>(texH);
                _imageScaleX = texW / displayW;
                _imageScaleY = texH / displayH;

                ImGui::TextUnformatted("Texture Preview:");
                // InvisibleButton captures mouse as an active ImGui item so the window is not dragged while selecting.
                ImGui::InvisibleButton("##tex_preview_drag", ImVec2(displayW, displayH));
                _imageOrigin = ImGui::GetItemRectMin();
                const ImVec2 imageMax(_imageOrigin.x + displayW, _imageOrigin.y + displayH);

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImTextureID texId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(_previewTexture->GetNativeHandle()));
                drawList->AddImage(texId, _imageOrigin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                if (!packed) {
                    _slicePanel.DrawOverlay(drawList, entry.texturePath, _imageOrigin, _imageScaleX, _imageScaleY);
                }

                if (!entry.srcRect.IsEmpty()) {
                    const ImVec2 rectMin(_imageOrigin.x + entry.srcRect.x / _imageScaleX, _imageOrigin.y + entry.srcRect.y / _imageScaleY);
                    const ImVec2 rectMax(rectMin.x + entry.srcRect.w / _imageScaleX, rectMin.y + entry.srcRect.h / _imageScaleY);
                    drawList->AddRect(rectMin, rectMax, ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)), 0.0f, 0, 2.0f);
                }

                // Drag-to-select: start when preview item is activated (click), track while dragging, finish on release.
                if (packed) {
                    _isDragging = false;
                }
                if (!packed && ImGui::IsItemActivated()) {
                    _isDragging = true;
                    const ImVec2 mouseScreenPos = ImGui::GetMousePos();
                    _dragStartPos.x = (mouseScreenPos.x - _imageOrigin.x) * _imageScaleX;
                    _dragStartPos.y = (mouseScreenPos.y - _imageOrigin.y) * _imageScaleY;
                    _dragEndPos = _dragStartPos;
                }

                if (_isDragging) {
                    const ImVec2 mouseScreenPos = ImGui::GetMousePos();
                    float texX = (mouseScreenPos.x - _imageOrigin.x) * _imageScaleX;
                    float texY = (mouseScreenPos.y - _imageOrigin.y) * _imageScaleY;
                    texX = eastl::clamp(texX, 0.0f, texW);
                    texY = eastl::clamp(texY, 0.0f, texH);
                    _dragEndPos.x = texX;
                    _dragEndPos.y = texY;

                    const ImVec2 dragScreenMin(_imageOrigin.x + eastl::min(_dragStartPos.x, _dragEndPos.x) / _imageScaleX, _imageOrigin.y + eastl::min(_dragStartPos.y, _dragEndPos.y) / _imageScaleY);
                    const ImVec2 dragScreenMax(_imageOrigin.x + eastl::max(_dragStartPos.x, _dragEndPos.x) / _imageScaleX, _imageOrigin.y + eastl::max(_dragStartPos.y, _dragEndPos.y) / _imageScaleY);
                    drawList->AddRectFilled(dragScreenMin, dragScreenMax, ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, 0.3f)));
                    drawList->AddRect(dragScreenMin, dragScreenMax, ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)), 0.0f, 0, 2.0f);

                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                        _isDragging = false;

                        const float minX = eastl::min(_dragStartPos.x, _dragEndPos.x);
                        const float minY = eastl::min(_dragStartPos.y, _dragEndPos.y);
                        const float maxX = eastl::max(_dragStartPos.x, _dragEndPos.x);
                        const float maxY = eastl::max(_dragStartPos.y, _dragEndPos.y);

                        entry.srcRect.x = minX;
                        entry.srcRect.y = minY;
                        entry.srcRect.w = maxX - minX;
                        entry.srcRect.h = maxY - minY;

                        lib.AddOrUpdate(entry);
                        _isDirty = true;
                    }
                }

                if (!packed) {
                    ImGui::TextUnformatted("Click and drag on the texture to select the sprite area.");
                }
            }
        }

        ImGui::Separator();

        ImGui::TextUnformatted("Source Rect (precise adjustment):");

        Rect rect = entry.srcRect;
        bool rectChanged = false;

        ImGui::BeginDisabled(packed);

        {
            const ScopedMonoFont mono;

            ImGui::PushItemWidth(70.0f);
            if (ImGui::DragFloat("X##sr_x", &rect.x, 0.5f, 0.0f)) {
                rectChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::DragFloat("Y##sr_y", &rect.y, 0.5f, 0.0f)) {
                rectChanged = true;
            }
            ImGui::PopItemWidth();

            ImGui::PushItemWidth(70.0f);
            if (ImGui::DragFloat("W##sr_w", &rect.w, 0.5f, 0.0f)) {
                rectChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::DragFloat("H##sr_h", &rect.h, 0.5f, 0.0f)) {
                rectChanged = true;
            }
            ImGui::PopItemWidth();
        }

        ImGui::EndDisabled();

        if (rectChanged) {
            entry.srcRect = rect;
            lib.AddOrUpdate(entry);
            _isDirty = true;
        }

        ImGui::Separator();

        if (sheetWidth > 0 && sheetHeight > 0 && !packed) {
            const PoolString firstAdded = _slicePanel.Render(lib, entry.texturePath, sheetWidth, sheetHeight);
            if (!firstAdded.Empty()) {
                _libraryPanel.ResetFilter();
                SelectByName(firstAdded);
                _isDirty = true;
                return;
            }
            ImGui::Separator();
        }

        if (_atlasPanel.Render(lib, entry)) {
            _textures.ForgetFailed();
            _isDirty = true;
            return;
        }
        ImGui::Separator();

        const PoolString previewClicked = _atlasPreviewPanel.Render(lib, entry, _atlasPanel.Settings(), _textures);
        if (!previewClicked.Empty()) {
            SelectByName(previewClicked);
            return;
        }
        ImGui::Separator();

        if (IconTextButton(ICON_LC_TRASH_2, "Delete Entry", "Remove this sprite from the library")) {
            lib.Remove(entry.name);
            Select(eastl::min(_selectedIndex, static_cast<int>(lib.GetAll().size()) - 1));
            _isDirty = true;
        }
    }

    void TextureEditorWidget::RefreshAssetList() {
        static const StringView kExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};
        _cachedAssets = CoreManager::GetFileSystem().EnumerateFiles(MountPoints::assets, Span<const StringView>(kExtensions));
        _assetsLoaded = true;
    }

}  // namespace BECore
