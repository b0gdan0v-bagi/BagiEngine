#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// A grid has 1 to this many columns and as many rows
    inline constexpr int kMaxSheetGridAxis = 64;
    inline constexpr int kMaxSheetGridCells = 1024;

    /// How a sheet is cut: `columns` x `rows` cells inside a border of `margin` pixels, `spacing` pixels between neighbours
    struct SheetGridSpec {
        int columns = 1;
        int rows = 1;
        int margin = 0;
        int spacing = 0;
    };

    CORE_ENUM(SheetGridError, uint8_t, NoSheet, BadCount, TooManyCells, CellTooSmall, DuplicateName, BadName)

    /// One line for the panel's status
    [[nodiscard]] StringView SheetGridErrorText(SheetGridError error);

    /**
     * @brief The cells of a @p sheetWidth x @p sheetHeight sheet, row-major from the top left, in whole pixels.
     *
     * Each axis divides what is left after the margins and the spacings by integer arithmetic, so the cells tile it with nothing
     * overlapping and nothing left over, and the widths (heights) differ by at most one pixel. A negative margin or spacing is
     * `CellTooSmall`.
     */
    [[nodiscard]] std::expected<Vector<Rect>, SheetGridError> ComputeGridCells(int sheetWidth, int sheetHeight, const SheetGridSpec& spec);

    /// Splits @p text at whitespace and commas; empty pieces are dropped
    [[nodiscard]] Vector<String> ParseSheetNames(StringView text);

    /// What the cells are called: cell `k` is `names[k]` when it has one, else `<prefix>_<startIndex + k>` zero-padded to the width of the last index
    struct SheetGridNaming {
        String prefix;
        uint32_t startIndex = 0;
        Vector<String> names;
    };

    /// What happens to a name the library already holds
    CORE_ENUM(SheetConflictPolicy, uint8_t, Overwrite, Skip)

    /// What a slice would do; building it changes nothing
    struct GridSlicePlan {
        Vector<SpriteEntry> entries;  // in cell order, without the skipped ones
        Vector<PoolString> replaced;  // names in `entries` that overwrite an entry of the library
        uint32_t skipped = 0;         // cells whose name the library holds, under `Skip`
    };

    /// The plan to cut @p texturePath. Every name is a valid sprite name and none repeats, or the error says which kind of mistake it is
    [[nodiscard]] std::expected<GridSlicePlan, SheetGridError> PlanGridSlice(const TextureLibrary& library, PoolString texturePath, int sheetWidth, int sheetHeight, const SheetGridSpec& spec,
                                                                             const SheetGridNaming& naming, SheetConflictPolicy policy);

    /// `AddOrUpdate` of every entry of @p plan: an entry the library holds keeps its place, new ones go to the end in cell order
    void ApplyGridSlice(TextureLibrary& library, const GridSlicePlan& plan);

}  // namespace BECore
