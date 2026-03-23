#include "TextureEditorWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/TextureEditorWidget.gen.hpp>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    bool TextureEditorWidget::Initialize(IDeserializer& /*deserializer*/) {
        return true;
    }

    void TextureEditorWidget::Update() {
        ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Sprite Library")) {
            ImGui::End();
            return;
        }

        // -----------------------------------------------------------------
        // Toolbar
        // -----------------------------------------------------------------
        ImGui::BeginDisabled(!_isDirty);
        if (ImGui::Button("Save")) {
            if (CoreManager::GetTextureLibrary().Save()) {
                _isDirty = false;
            }
        }
        ImGui::EndDisabled();

        if (_isDirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved changes)");
        }

        ImGui::Separator();

        // -----------------------------------------------------------------
        // Two-panel split
        // -----------------------------------------------------------------
        const float leftWidth = 200.0f;
        ImGui::BeginChild("##sprite_list", ImVec2(leftWidth, 0.0f), true);
        RenderLeftPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##sprite_edit", ImVec2(0.0f, 0.0f), true);
        RenderRightPanel();
        ImGui::EndChild();

        ImGui::End();
    }

    void TextureEditorWidget::Draw() {}

    // -------------------------------------------------------------------------

    void TextureEditorWidget::RenderLeftPanel() {
        auto& lib = CoreManager::GetTextureLibrary();
        const auto& entries = lib.GetAll();

        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const bool selected = (_selectedIndex == i);
            if (ImGui::Selectable(entries[i].name.CStr(), selected)) {
                _selectedIndex = i;
                _nameDirty = false;
                const size_t copyLen = eastl::min(sizeof(_nameEditBuffer) - 1, strlen(entries[i].name.CStr()));
                memcpy(_nameEditBuffer, entries[i].name.CStr(), copyLen);
                _nameEditBuffer[copyLen] = '\0';
            }
        }

        ImGui::Separator();
        if (ImGui::Button("[+ Add]")) {
            SpriteEntry newEntry;
            newEntry.name = PoolString::Intern("new_sprite");
            lib.AddOrUpdate(eastl::move(newEntry));
            _selectedIndex = static_cast<int>(lib.GetAll().size()) - 1;
            const auto& addedEntry = lib.GetAll()[static_cast<size_t>(_selectedIndex)];
            const size_t copyLen = eastl::min(sizeof(_nameEditBuffer) - 1, strlen(addedEntry.name.CStr()));
            memcpy(_nameEditBuffer, addedEntry.name.CStr(), copyLen);
            _nameEditBuffer[copyLen] = '\0';
            _nameDirty = false;
            _isDirty = true;
        }
    }

    void TextureEditorWidget::RenderRightPanel() {
        auto& lib = CoreManager::GetTextureLibrary();
        const auto& entries = lib.GetAll();

        if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(entries.size())) {
            ImGui::TextDisabled("Select a sprite from the list");
            return;
        }

        SpriteEntry entry = entries[static_cast<size_t>(_selectedIndex)];

        // -----------------------------------------------------------------
        // Name field
        // -----------------------------------------------------------------
        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##sprite_name", _nameEditBuffer, sizeof(_nameEditBuffer))) {
            _nameDirty = true;
        }
        if (_nameDirty && ImGui::IsItemDeactivatedAfterEdit()) {
            PoolString newName = PoolString::Intern(eastl::string_view(_nameEditBuffer));
            if (newName != entry.name) {
                lib.Remove(entry.name);
                entry.name = newName;
                lib.AddOrUpdate(entry);
                _selectedIndex = static_cast<int>(lib.GetAll().size()) - 1;
                for (int i = 0; i < static_cast<int>(lib.GetAll().size()); ++i) {
                    if (lib.GetAll()[static_cast<size_t>(i)].name == newName) {
                        _selectedIndex = i;
                        break;
                    }
                }
                _isDirty = true;
            }
            _nameDirty = false;
        }

        ImGui::Separator();

        // -----------------------------------------------------------------
        // Texture file combo
        // -----------------------------------------------------------------
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

        ImGui::SameLine();
        if (ImGui::Button("Refresh##tex")) {
            _assetsLoaded = false;
        }

        ImGui::Separator();

        // -----------------------------------------------------------------
        // Texture preview with drag-to-select
        // -----------------------------------------------------------------
        if (!entry.texturePath.Empty()) {
            // Load texture if path changed
            if (entry.texturePath != _previewLoadedPath) {
                _previewTexture = CoreManager::GetResourceManager().Load<ITexture>(entry.texturePath.ToStringView());
                _previewLoadedPath = entry.texturePath;
                _isDragging = false;
            }

            // Display texture preview if loaded
            if (_previewTexture && _previewTexture->GetState() == ResourceState::Loaded) {
                const float texW = _previewTexture->GetWidth();
                const float texH = _previewTexture->GetHeight();
                const float maxDisplaySize = 400.0f;

                // Calculate display size preserving aspect ratio
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

                // Calculate scales for coordinate conversion
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

                // Draw current srcRect as overlay
                if (!entry.srcRect.IsEmpty()) {
                    const ImVec2 rectMin(_imageOrigin.x + entry.srcRect.x / _imageScaleX, _imageOrigin.y + entry.srcRect.y / _imageScaleY);
                    const ImVec2 rectMax(rectMin.x + entry.srcRect.w / _imageScaleX, rectMin.y + entry.srcRect.h / _imageScaleY);
                    drawList->AddRect(rectMin, rectMax, ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)), 0.0f, 0, 2.0f);
                }

                // Drag-to-select: start when preview item is activated (click), track while dragging, finish on release.
                if (ImGui::IsItemActivated()) {
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

                    // Draw preview rectangle during drag
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

                ImGui::TextUnformatted("Click and drag on the texture to select the sprite area.");
            }
        }

        ImGui::Separator();

        // -----------------------------------------------------------------
        // srcRect editing
        // -----------------------------------------------------------------
        ImGui::TextUnformatted("Source Rect (precise adjustment):");

        Rect rect = entry.srcRect;
        bool rectChanged = false;

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

        if (rectChanged) {
            entry.srcRect = rect;
            lib.AddOrUpdate(entry);
            _isDirty = true;
        }

        ImGui::Separator();

        // -----------------------------------------------------------------
        // Delete button
        // -----------------------------------------------------------------
        if (ImGui::Button("Delete Entry")) {
            lib.Remove(entry.name);
            _selectedIndex = eastl::min(_selectedIndex, static_cast<int>(lib.GetAll().size()) - 1);
            _isDirty = true;
        }
    }

    void TextureEditorWidget::RefreshAssetList() {
        static const eastl::string_view kExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};
        _cachedAssets = CoreManager::GetFileSystem().EnumerateFiles("assets"_intern, eastl::span<const eastl::string_view>(kExtensions));
        _assetsLoaded = true;
    }

}  // namespace BECore
