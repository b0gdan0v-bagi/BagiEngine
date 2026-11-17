#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/TextureEditor/SpriteAtlasPanel.h>
#include <Widgets/TextureEditor/SpriteLibraryPanel.h>
#include <Widgets/TextureEditor/SpriteSheetSlicePanel.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <imgui.h>

namespace BECore {

    /**
     * @brief ImGui panel for the TextureLibrary sprite definitions
     *
     * Left: the entries, each with a thumbnail, its texture and an atlas badge, filtered by atlas, and [+ Add]. Right: the selected entry (name, texture file, srcRect with a
     * drag-to-select preview), a "Slice sheet" section that cuts the texture into one entry per grid cell or alpha island, an "Atlas"
     * section that packs a group of sprites into one texture and unpacks it (a packed sprite's texture and rect are read-only), and
     * [Delete]. [Save] persists the changes to config/TextureLibrary.xml.
     */
    class TextureEditorWidget : public IWidget {
        BE_CLASS(TextureEditorWidget)
    public:
        TextureEditorWidget() = default;
        ~TextureEditorWidget() override = default;

        bool Initialize(IDeserializer& deserializer) override;
        void Update() override;
        void Draw() override;

    private:
        // Runs `texture.save` like the Save button (only with unsaved changes)
        void OnCommand(const EditorCommandEvent& event);
        // The one path of the button and the command
        void SaveLibrary();

        void RenderLeftPanel();
        void RenderRightPanel();
        void RefreshAssetList();
        // Selects the entry called @p name, as a click in the list does; nothing when there is none
        void SelectByName(PoolString name);
        // The one way to select: remembers the index and loads the name into the edit field; nothing for an index outside the library
        void SelectIndex(int index);

        int _selectedIndex = -1;
        bool _isDirty = false;

        Vector<PoolString> _cachedAssets;
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

        SpriteTextureCache _textures;
        SpriteLibraryPanel _libraryPanel;
        SpriteSheetSlicePanel _slicePanel;
        SpriteAtlasPanel _atlasPanel;
    };

}  // namespace BECore
