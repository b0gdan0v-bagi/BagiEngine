#pragma once

#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/TextureEditor/SpriteLibraryView.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    /// Squares of @p cell pixels, alternating @p light and @p dark, over the box: what a transparent sprite is drawn on. The last squares are cut at the edge.
    void DrawCheckerRect(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 light, ImU32 dark, float cell);

    /// A generated variant released on the list: on the sprite of library index `row`, or on the free area (`row` -1)
    struct SpriteLibraryDrop {
        int row = -1;
        ArtGenDragPayload payload;
    };

    /**
     * @brief The left list of the Sprite Library: an "Atlas" filter over a list of sprites, each with a thumbnail, the texture it is
     * on and a badge for its atlas state. The rules are `SpriteLibraryView`'s; this draws what they say.
     *
     * The list is made again only when `LibraryListKey` or the filter moved, and it is drawn through a clipper, so only the visible rows
     * load a texture. Texts go to stack buffers or ImGui's own, so a steady frame allocates nothing. The selection is the widget's:
     * a selected sprite the filter hides is still the selected one.
     *
     * It is also the drop target of a dragged variant (`ArtGenDrag.h`): it only reports where the payload was released (`TakeDrop`) and
     * makes no accept. The free area is submitted before the rows, because the last target under the pointer wins and a row must beat it.
     */
    class SpriteLibraryPanel {
    public:
        /// @return The library index of the row clicked this frame, or -1
        int Render(const TextureLibrary& library, int selectedIndex, SpriteTextureCache& textures);

        /// @return The drop released on the list since the last call, once. A sound is never a drop: the list says why it takes none
        [[nodiscard]] Optional<SpriteLibraryDrop> TakeDrop();

        /// Back to "All sprites": the widget calls it when it adds a sprite or a slice adds some, so the new one is seen
        void ResetFilter();

    private:
        void EnsureBuilt(const TextureLibrary& library);
        void DrawFilterCombo();
        /// @return Whether the row was clicked
        bool DrawRow(const SpriteEntry& entry, bool selected, SpriteTextureCache& textures, int index);
        /// The target of the list below the filter combo; called before any row
        void SubmitDropArea();
        /// The tint and the words of the free area while a variant is over it and over no row
        void DrawDropHint() const;

        static constexpr size_t kLabelSize = 128;

        AtlasFilter _filter;
        AtlasFilter _builtFilter;
        bool _built = false;
        uint64_t _builtKey = 0;
        Vector<AtlasGroupInfo> _groups;
        Vector<uint32_t> _rows;
        uint32_t _noAtlasCount = 0;
        uint32_t _spriteCount = 0;
        char _filterLabel[kLabelSize] = {};

        Optional<SpriteLibraryDrop> _drop;
        ImVec2 _areaMin{};
        ImVec2 _areaMax{};
        bool _areaHot = false;  // a variant is over the free area this frame ...
        bool _rowHot = false;   // ... and a row under it takes it instead
    };

}  // namespace BECore
