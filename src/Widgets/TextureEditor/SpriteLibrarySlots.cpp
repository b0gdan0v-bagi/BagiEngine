#include "SpriteLibrarySlots.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Widgets/TextureEditor/SpriteAtlas.h>
#include <Widgets/TextureEditor/SpriteLibraryPanelModel.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>

namespace BECore {

    namespace {

        constexpr StringView kListSlotName = "spriteList";
        constexpr StringView kPreviewSlotName = "spritePreview";

        /// The longer side of the preview in screen pixels; a smaller texture is drawn at its own size
        constexpr float kPreviewMaxSize = 400.0f;
        constexpr float kRectThickness = 2.0f;
        const ImVec4 kRectColor(0.0f, 1.0f, 0.0f, 1.0f);
        const ImVec4 kDragFillColor(0.0f, 1.0f, 0.0f, 0.3f);

    }  // namespace

    void SpriteLibrarySlots::Register() {
        PanelSlots& slots = PanelSlots::GetInstance();
        // The closures keep `this` only: they are called inside the frame and the handles end with this object
        _listSlot = slots.Register(kListSlotName, [this] { DrawList(); });
        _previewSlot = slots.Register(kPreviewSlotName, [this] { DrawPreview(); });
    }

    void SpriteLibrarySlots::DrawList() {
        SpriteLibraryPanel& list = _model.GetLibraryPanel();
        const int clicked = list.Render(_model.GetLibrary(), _model.GetSelectedIndex(), _model.GetTextureCache());
        if (clicked >= 0) {
            _model.NoteListClick(clicked);
        }
        if (const Optional<SpriteLibraryDrop> drop = list.TakeDrop()) {
            _model.NoteDrop(*drop);
        }
    }

    void SpriteLibrarySlots::DrawPreview() {
        const SpriteEntry* entry = _model.GetSelected();
        if (entry == nullptr || entry->texturePath.Empty()) {
            _model.NoteSheet({}, 0, 0);
            return;
        }
        // A file rewritten on disk is evicted from the cache (the Generate panel's Accept and switch do): the handle held here is the old picture
        ResourceManager& resources = CoreManager::GetResourceManager();
        const bool evicted = IsEvictedHandle(resources.GetCache(), _previewLoadedPath, _previewTexture.Get());
        if (entry->texturePath != _previewLoadedPath || evicted) {
            _previewTexture = resources.Load<ITexture>(entry->texturePath.ToStringView());
            _previewLoadedPath = entry->texturePath;
            _isDragging = false;
        }
        if (!_previewTexture || _previewTexture->GetState() != ResourceState::Loaded) {
            _model.NoteSheet({}, 0, 0);
            return;
        }

        const float textureWidth = _previewTexture->GetWidth();
        const float textureHeight = _previewTexture->GetHeight();
        float displayWidth = textureWidth;
        float displayHeight = textureHeight;
        if (displayWidth > kPreviewMaxSize || displayHeight > kPreviewMaxSize) {
            const float aspectRatio = textureWidth / textureHeight;
            if (textureWidth > textureHeight) {
                displayWidth = kPreviewMaxSize;
                displayHeight = kPreviewMaxSize / aspectRatio;
            } else {
                displayHeight = kPreviewMaxSize;
                displayWidth = kPreviewMaxSize * aspectRatio;
            }
        }
        _model.NoteSheet(entry->texturePath, static_cast<int>(textureWidth), static_cast<int>(textureHeight));
        _imageScaleX = textureWidth / displayWidth;
        _imageScaleY = textureHeight / displayHeight;
        const bool packed = IsPackedSprite(*entry);

        ImGui::TextUnformatted("Texture Preview:");
        // An active item takes the mouse, so the window is not dragged while selecting
        ImGui::InvisibleButton("##tex_preview_drag", ImVec2(displayWidth, displayHeight));
        _imageOrigin = ImGui::GetItemRectMin();
        const ImVec2 imageMax(_imageOrigin.x + displayWidth, _imageOrigin.y + displayHeight);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImTextureID textureId = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(_previewTexture->GetNativeHandle()));
        drawList->AddImage(textureId, _imageOrigin, imageMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        if (!packed && _model.GetView().slice.open) {
            _model.GetSlicePanel().DrawOverlay(drawList, entry->texturePath, _imageOrigin, _imageScaleX, _imageScaleY);
        }

        const Rect& rect = entry->srcRect;
        if (!rect.IsEmpty()) {
            const ImVec2 rectMin(_imageOrigin.x + rect.x / _imageScaleX, _imageOrigin.y + rect.y / _imageScaleY);
            const ImVec2 rectMax(rectMin.x + rect.w / _imageScaleX, rectMin.y + rect.h / _imageScaleY);
            drawList->AddRect(rectMin, rectMax, ImGui::GetColorU32(kRectColor), 0.0f, 0, kRectThickness);
        }

        // A packed sprite's rect points into its atlas: it waits for Unpack
        if (packed) {
            _isDragging = false;
            return;
        }
        DrawDrag(*drawList, textureWidth, textureHeight);
        ImGui::TextUnformatted("Click and drag on the texture to select the sprite area.");
    }

    void SpriteLibrarySlots::DrawDrag(ImDrawList& drawList, float textureWidth, float textureHeight) {
        if (ImGui::IsItemActivated()) {
            _isDragging = true;
            const ImVec2 mouse = ImGui::GetMousePos();
            _dragStart = ImVec2((mouse.x - _imageOrigin.x) * _imageScaleX, (mouse.y - _imageOrigin.y) * _imageScaleY);
            _dragEnd = _dragStart;
        }
        if (!_isDragging) {
            return;
        }
        const ImVec2 mouse = ImGui::GetMousePos();
        _dragEnd.x = std::clamp((mouse.x - _imageOrigin.x) * _imageScaleX, 0.0f, textureWidth);
        _dragEnd.y = std::clamp((mouse.y - _imageOrigin.y) * _imageScaleY, 0.0f, textureHeight);

        const float minX = std::min(_dragStart.x, _dragEnd.x);
        const float minY = std::min(_dragStart.y, _dragEnd.y);
        const float maxX = std::max(_dragStart.x, _dragEnd.x);
        const float maxY = std::max(_dragStart.y, _dragEnd.y);
        const ImVec2 screenMin(_imageOrigin.x + minX / _imageScaleX, _imageOrigin.y + minY / _imageScaleY);
        const ImVec2 screenMax(_imageOrigin.x + maxX / _imageScaleX, _imageOrigin.y + maxY / _imageScaleY);
        drawList.AddRectFilled(screenMin, screenMax, ImGui::GetColorU32(kDragFillColor));
        drawList.AddRect(screenMin, screenMax, ImGui::GetColorU32(kRectColor), 0.0f, 0, kRectThickness);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            _isDragging = false;
            _model.NoteDraggedRect(Rect(minX, minY, maxX - minX, maxY - minY));
        }
    }

}  // namespace BECore
