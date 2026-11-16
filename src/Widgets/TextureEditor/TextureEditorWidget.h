#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <imgui.h>

namespace BECore {

    /**
     * @brief ImGui panel for the TextureLibrary sprite definitions
     *
     * Left: the list of named entries and [+ Add]. Right: the selected entry (name, texture file, srcRect with a
     * drag-to-select preview) and [Delete]. [Save] persists the changes to config/TextureLibrary.xml.
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
        bool _open = true;  // the window's close button and its `view.*` command; a closed panel is skipped like a hidden dock tab
        // Runs `texture.save` like the Save button (only with unsaved changes)
        void OnCommand(const EditorCommandEvent& event);
        // The one path of the button and the command
        void SaveLibrary();

        // The one place that changes the selection: the Name field follows it and a refused rename is forgotten.
        // An index outside the list selects nothing.
        void Select(int index);
        void SelectByName(PoolString name);

        void RenderLeftPanel();
        void RenderRightPanel();
        void RefreshAssetList();

        int _selectedIndex = -1;
        bool _isDirty = false;

        Vector<PoolString> _cachedAssets;
        bool _assetsLoaded = false;

        char _nameEditBuffer[256] = {};
        bool _nameDirty = false;
        // The name a rename was refused for (a sprite has it already); the panel warns until the selection or the typed name changes
        PoolString _renameRefused;

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
