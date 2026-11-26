#pragma once

#include <BECore/PoolString/PoolString.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <cstdint>

struct ImVec2;

namespace BECore {

    class AssetBrowserPanelModel;
    struct AssetFile;

    /**
     * @brief The drawers of `AssetBrowserPanel.xml` that the format cannot say: the folder tree and the grid of tiles
     *
     * Owned by `AssetBrowserPanelModel`, which registers them as the slots `assetFolders` and `assetGrid` in its `Initialize` and takes them back when it
     * dies. **A slot notes, the model applies**: a clicked folder or tile is handed to the model (`Note...`) and applied in its `EndUpdate`, because
     * applying it inside the walk would change the list that is being drawn. The thumbnails live here: only a tile in view loads its texture.
     */
    class AssetBrowserSlots {
    public:
        explicit AssetBrowserSlots(AssetBrowserPanelModel& model) : _model(model) {}

        /// Registers the slots in the global `PanelSlots`; the handles end with this object
        void Register();

        /// Lets go of every thumbnail (the panel closed); a later draw loads what it shows again
        void ReleaseThumbnails() {
            _thumbnails.Clear();
        }

    private:
        void DrawFolders();
        void DrawGrid();
        void DrawTile(const AssetFile& file, const ImVec2& size);
        void DrawPicture(const AssetFile& file, const ImVec2& min, float side);

        AssetBrowserPanelModel& _model;
        SpriteTextureCache _thumbnails;
        uint32_t _catalogRevision = 0;  ///< the catalog the failed thumbnails were forgotten for

        PanelSlotHandle _folderSlot;
        PanelSlotHandle _gridSlot;
    };

}  // namespace BECore
