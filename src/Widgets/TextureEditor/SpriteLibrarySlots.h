#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Types/Types.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <imgui.h>

namespace BECore {

    class SpriteLibraryPanelModel;
    struct SpriteEntry;

    /**
     * @brief The drawers of `SpriteLibraryPanel.xml` that the format cannot say: the sprite list, the preview of the selected entry's texture and the
     * picture of the atlas preview
     *
     * Owned by `SpriteLibraryPanelModel`, which registers them as the slots `spriteList`, `spritePreview` and `spriteAtlasPreview` in its `Initialize` and
     * takes them back when it dies. **A slot notes, the model applies**: a clicked row, a drop, a finished drag and a clicked rect of the atlas preview are
     * handed to the model (`Note...`) and applied in its
     * `EndUpdate`, after the walk, because applying them inside it would change the lists the interpreter is reading. The preview keeps its texture
     * handle and the drag here; the selection, the library and the list's state are the model's.
     */
    class SpriteLibrarySlots {
    public:
        explicit SpriteLibrarySlots(SpriteLibraryPanelModel& model) : _model(model) {}

        /// Registers the slots in the global `PanelSlots`; the handles end with this object
        void Register();

    private:
        void DrawList();
        void DrawPreview();
        void DrawAtlasPreview();
        /// The drag-to-select over the image just drawn: a rect in texture pixels is noted on release
        void DrawDrag(ImDrawList& drawList, float textureWidth, float textureHeight);

        SpriteLibraryPanelModel& _model;
        ResourceHandle<ITexture> _previewTexture;
        PoolString _previewLoadedPath;
        bool _isDragging = false;
        ImVec2 _dragStart{};  // texture pixels
        ImVec2 _dragEnd{};
        ImVec2 _imageOrigin{};
        float _imageScaleX = 1.0f;
        float _imageScaleY = 1.0f;

        PanelSlotHandle _listSlot;
        PanelSlotHandle _previewSlot;
        PanelSlotHandle _atlasPreviewSlot;
    };

}  // namespace BECore
