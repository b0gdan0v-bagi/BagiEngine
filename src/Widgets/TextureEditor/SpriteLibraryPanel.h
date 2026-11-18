#pragma once

#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <Widgets/TextureEditor/SpriteLibraryView.h>
#include <Widgets/TextureEditor/SpriteTextureCache.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    /// Squares of @p cell pixels, alternating @p light and @p dark, over the box: what a transparent sprite is drawn on. The last squares are cut at the edge.
    void DrawCheckerRect(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 light, ImU32 dark, float cell);

    /**
     * @brief The left list of the Sprite Library: an "Atlas" filter over a list of sprites, each with a thumbnail, the texture it is
     * on and a badge for its atlas state. The rules are `SpriteLibraryView`'s; this draws what they say.
     *
     * The list is made again only when `LibraryListKey` or the filter moved, and it is drawn through a clipper, so only the visible rows
     * load a texture. Texts go to stack buffers or ImGui's own, so a steady frame allocates nothing. The selection is the widget's:
     * a selected sprite the filter hides is still the selected one.
     */
    class SpriteLibraryPanel {
    public:
        /// @return The library index of the row clicked this frame, or -1
        int Render(const TextureLibrary& library, int selectedIndex, SpriteTextureCache& textures);

        /// Back to "All sprites": the widget calls it when it adds a sprite or a slice adds some, so the new one is seen
        void ResetFilter();

    private:
        void EnsureBuilt(const TextureLibrary& library);
        void DrawFilterCombo();
        /// @return Whether the row was clicked
        bool DrawRow(const SpriteEntry& entry, bool selected, SpriteTextureCache& textures, int index);

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
    };

}  // namespace BECore
