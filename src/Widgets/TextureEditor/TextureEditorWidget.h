#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Widgets/IWidget.h>
#include <EASTL/vector.h>
#include <imgui.h>

namespace BECore {

    /**
     * @brief ImGui widget for managing the TextureLibrary sprite definitions
     *
     * Two-panel layout:
     *   Left  - Scrollable list of all named sprite entries; [+ Add] button
     *   Right - Edit the selected entry (name, texture file, srcRect); [Delete] button
     *
     * Right panel now includes interactive texture preview with drag-to-select for srcRect.
     *
     * [Save] persists changes to config/TextureLibrary.xml.
     */
    class TextureEditorWidget : public IWidget {
        BE_CLASS(TextureEditorWidget)
    public:
        TextureEditorWidget() = default;
        ~TextureEditorWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        void RenderLeftPanel();
        void RenderRightPanel();
        void RefreshAssetList();

        int _selectedIndex = -1;
        bool _isDirty = false;

        eastl::vector<PoolString> _cachedAssets;
        bool _assetsLoaded = false;

        char _nameEditBuffer[256] = {};
        bool _nameDirty = false;

        // Texture preview and drag state
        ResourceHandle<ITexture> _previewTexture;
        PoolString _previewLoadedPath;
        bool _isDragging = false;
        ImVec2 _dragStartPos{};
        ImVec2 _dragEndPos{};
        ImVec2 _imageOrigin{};
        float _imageScaleX = 1.0f;
        float _imageScaleY = 1.0f;
    };

}  // namespace BECore
