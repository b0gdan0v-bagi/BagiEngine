#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Widgets/TextureEditor/SpriteSheetGrid.h>
#include <Widgets/TextureEditor/SpriteSheetIslands.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    class TextureLibrary;

    /// Where the rects of a slice come from
    CORE_ENUM(SheetSliceMode, uint8_t, Grid, Islands)

    /**
     * @brief The "Slice sheet" section as the Sprite Library asset binds it (`sprites.slice`)
     *
     * The fields of the two modes, the names and the conflict choice are written by the controls and taken by `SpriteSheetSlicePanel::Sync`, which
     * clamps them in place; `open` is the section's `openBind`. `canSlice`, `summary`, `hasError` and `canAdd` are written by the model.
     */
    struct SpriteSliceView {
        BE_CLASS(SpriteSliceView)

        BE_REFLECT_FIELD SheetSliceMode mode = SheetSliceMode::Grid;
        BE_REFLECT_FIELD int32_t columns = 6;
        BE_REFLECT_FIELD int32_t rows = 2;
        BE_REFLECT_FIELD int32_t margin = 0;
        BE_REFLECT_FIELD int32_t spacing = 0;
        BE_REFLECT_FIELD int32_t alphaThreshold = kDefaultIslandAlphaThreshold;
        BE_REFLECT_FIELD int32_t minSize = kDefaultIslandMinSize;
        BE_REFLECT_FIELD int32_t padding = kDefaultIslandPadding;
        BE_REFLECT_FIELD int32_t mergeGap = kDefaultIslandMergeGap;
        BE_REFLECT_FIELD String prefix = "sprite";
        BE_REFLECT_FIELD int32_t startIndex = 0;
        /// Optional names in order, separated by spaces or commas
        BE_REFLECT_FIELD String names;
        BE_REFLECT_FIELD SheetConflictPolicy policy = SheetConflictPolicy::Overwrite;
        /// The section is open: the cells are drawn over the preview only then
        BE_REFLECT_FIELD bool open = false;
        /// The selected entry has a loaded texture and is not packed: the section shows only then
        BE_REFLECT_FIELD bool canSlice = false;
        /// What Add sprites would do, or why it cannot (`hasError`)
        BE_REFLECT_FIELD String summary;
        BE_REFLECT_FIELD bool hasError = false;
        BE_REFLECT_FIELD bool canAdd = false;
    };

    /**
     * @brief The "Slice sheet" section of the Sprite Library panel: a mode (a grid, or the alpha islands of the file), the fields of
     * the mode, the names, the conflict choice, a summary of what a click would do and the button that does it. The math and the
     * rules are SpriteSheetGrid's and SpriteSheetIslands'; this holds the section's state over them, takes its fields from `Sync` (the asset's
     * `SpriteSliceView`) and draws only the overlay.
     *
     * Both modes end with a list of rects (`_cells`) and one plan made from it. The plan is made again only when an input, the
     * texture or the names in the library changed, so a steady frame allocates nothing. The islands are found only when their button
     * is pressed (it reads and scans the file); an edited island field, another texture or another size clears the result, so the
     * overlay never shows an out-of-date one.
     */
    class SpriteSheetSlicePanel {
    public:
        /// The ranges of the fields (`Sync` clamps to them); a margin or spacing beyond `kMaxInsetPixels` is a typo, no texture is that big
        static constexpr int kMaxInsetPixels = 16384;
        static constexpr int kMaxStartIndex = 100000;
        static constexpr int kMaxIslandMinSize = 16384;
        /// The click and Ctrl-click steps of the fields, which the asset's steppers must repeat: counts (columns, rows, the first number), pixels, alpha
        static constexpr int kCountStep = 1;
        static constexpr int kCountStepFast = 4;
        static constexpr int kPixelStep = 1;
        static constexpr int kPixelStepFast = 8;
        static constexpr int kAlphaStep = 1;
        static constexpr int kAlphaStepFast = 16;

        /**
         * @brief Takes the fields of @p view for the sheet @p texturePath of @p sheetWidth x @p sheetHeight pixels.
         *
         * The numbers are clamped and the prefix and the names cut to the old field sizes, in the view too. A change of the mode, of an island field, of
         * the texture or of its size clears the islands found. The plan is made again only when a field, the texture or the names in the library changed,
         * and only then are `summary`, `hasError` and `canAdd` written, so a steady call allocates nothing.
         */
        void Sync(SpriteSliceView& view, const TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight);

        /// Reads the file of @p texturePath and finds its islands (the Find islands button); the result is planned by the next `Sync`
        void FindIslands(PoolString texturePath, int width, int height);

        /**
         * @brief Adds the planned sprites to @p library (the Add sprites button).
         * @return The name of the first sprite added, or an empty name when there was no plan to add.
         */
        PoolString AddPlanned(TextureLibrary& library);

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
        void ClearIslands();
        void SetStatus(StringView text, bool isError);

        static constexpr size_t kPrefixSize = 65;
        static constexpr size_t kNamesSize = 2048;
        static constexpr size_t kSummarySize = 160;

        SheetSliceMode _mode = SheetSliceMode::Grid;
        SheetGridSpec _spec{6, 2, 0, 0};
        IslandSpec _island;
        int _startIndex = 0;
        SheetConflictPolicy _policy = SheetConflictPolicy::Overwrite;
        char _prefix[kPrefixSize] = "sprite";
        char _names[kNamesSize] = {};

        bool _isOpen = false;
        bool _inputsChanged = true;
        PlanKey _key;
        Vector<Rect> _cells;  // the grid's cells, or the islands found by the last press; empty when there are none
        String _islandError;  // why the last press found nothing; empty when it has not failed
        GridSlicePlan _plan;
        bool _hasPlan = false;
        bool _hasError = false;
        char _summary[kSummarySize] = {};
    };

}  // namespace BECore
