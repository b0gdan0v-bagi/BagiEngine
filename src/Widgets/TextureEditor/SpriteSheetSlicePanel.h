#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <Widgets/TextureEditor/SpriteSheetGrid.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    class TextureLibrary;

    /**
     * @brief The "Slice sheet" section of the Sprite Library panel: the grid fields, the names, the conflict choice, a summary of
     * what a click would do and the button that does it. The math and the rules are SpriteSheetGrid's; this is the ImGui over them.
     *
     * The plan is made again only when an input, the texture or the names in the library changed, so a steady frame allocates nothing.
     */
    class SpriteSheetSlicePanel {
    public:
        /**
         * @brief Draws the section for the sheet @p texturePath of @p sheetWidth x @p sheetHeight pixels.
         * @return The name of the first sprite added when the button was pressed this frame, else an empty name.
         */
        PoolString Render(TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight);

        /// Draws the cells of the open section over the preview of @p texturePath: @p origin is the image's corner, @p scaleX and
        /// @p scaleY the texture pixels per screen pixel. Nothing while the section is closed or its grid is not valid.
        void DrawOverlay(ImDrawList* drawList, PoolString texturePath, ImVec2 origin, float scaleX, float scaleY) const;

    private:
        /// What a plan was made for, apart from the fields the user edits
        struct PlanKey {
            PoolString texturePath;
            int width = 0;
            int height = 0;
            uint64_t libraryNames = 0;

            bool operator==(const PlanKey&) const = default;
        };

        void MakePlan(const TextureLibrary& library);

        static constexpr size_t kPrefixSize = 65;
        static constexpr size_t kNamesSize = 2048;
        static constexpr size_t kSummarySize = 160;

        SheetGridSpec _spec{6, 2, 0, 0};
        int _startIndex = 0;
        SheetConflictPolicy _policy = SheetConflictPolicy::Overwrite;
        char _prefix[kPrefixSize] = "sprite";
        char _names[kNamesSize] = {};

        bool _isOpen = false;
        bool _inputsChanged = true;
        PlanKey _key;
        Vector<Rect> _cells;  // of the last valid grid; empty when the grid is not valid
        GridSlicePlan _plan;
        bool _hasPlan = false;
        bool _hasError = false;
        char _summary[kSummarySize] = {};
    };

}  // namespace BECore
