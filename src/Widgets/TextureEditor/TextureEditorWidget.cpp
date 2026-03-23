#include "TextureEditorWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Generated/TextureEditorWidget.gen.hpp>
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
        // srcRect editing
        // -----------------------------------------------------------------
        ImGui::TextUnformatted("Source Rect:");

        Rect rect = entry.srcRect;
        bool rectChanged = false;

        ImGui::PushItemWidth(70.0f);
        if (ImGui::DragFloat("X##sr_x", &rect.x, 0.5f)) {
            rectChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::DragFloat("Y##sr_y", &rect.y, 0.5f)) {
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
