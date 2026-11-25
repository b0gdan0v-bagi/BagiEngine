#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/EditorDocuments.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/ArtGen/Sets/ArtGenSet.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/TextureEditor/SpriteAtlasPanel.h>
#include <Widgets/TextureEditor/SpriteAtlasPreviewPanel.h>
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
     * section that packs a group of sprites into one texture and unpacks it (a packed sprite's texture and rect are read-only), an "Atlas
     * preview" of the group's atlas (what is packed, or what Pack would make; a click on a rect selects that sprite), and [Delete]. [Save] persists the changes to config/TextureLibrary.xml.
     */
    class TextureEditorWidget : public IWidget {
        BE_CLASS(TextureEditorWidget)
    public:
        TextureEditorWidget() = default;
        ~TextureEditorWidget() override = default;

        bool Initialize(IDeserializer& deserializer) override;

        /// The Sprite Library with its unsaved flag, for `editor_state` (`.claude/rules/control-editor-api.md`). A closed panel reports too
        void AppendDocuments(Vector<EditorDocumentState>& out);
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

        // A generated variant released on the library list (ArtGenDrag.h): a sprite row replaces that sprite at once, the free area asks for a name first
        void OnLibraryDrop(const SpriteLibraryDrop& drop);
        void DrawDropPopup();
        // The Generate panel does the accept (it owns the form's settings): this asks through the event and shows what it answers
        void AcceptDrop(const ArtGenDragPayload& payload, StringView sprite);
        void PlanPopupName();

        void RenderLeftPanel();
        void RenderRightPanel();
        void RefreshAssetList();

        int _selectedIndex = -1;
        bool _isDirty = false;
        EditorDocumentsHandle _documents;  // the source of `editor_state`, ends with the widget

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

        // The last drop's answer, made at the drop and drawn under the Save row
        String _dropMessage;
        bool _dropFailed = false;
        // A drop on the free area waits for its name in the popup
        ArtGenDragPayload _pendingDrop;
        ArtGenDropPlan _popupPlan;
        char _popupName[kArtGenRowNameSize] = {};
        bool _openDropPopup = false;

        SpriteTextureCache _textures;
        SpriteLibraryPanel _libraryPanel;
        SpriteSheetSlicePanel _slicePanel;
        SpriteAtlasPanel _atlasPanel;
        SpriteAtlasPreviewPanel _atlasPreviewPanel;
    };

}  // namespace BECore
