#include <BECore/Resource/TextureLibrary.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/TextureEditor/SpriteSheetGrid.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kChessSheetWidth = 700;
        constexpr int kChessSheetHeight = 381;

        SpriteEntry MakeEntry(const char* name, const char* path, Rect rect) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = rect;
            return entry;
        }

        SheetGridSpec Grid(int columns, int rows, int margin = 0, int spacing = 0) {
            SheetGridSpec spec;
            spec.columns = columns;
            spec.rows = rows;
            spec.margin = margin;
            spec.spacing = spacing;
            return spec;
        }

        SheetGridNaming Named(const char* prefix, uint32_t startIndex = 0) {
            SheetGridNaming naming;
            naming.prefix = prefix;
            naming.startIndex = startIndex;
            return naming;
        }

        void ExpectRect(const Rect& actual, const Rect& expected) {
            EXPECT_EQ(actual.x, expected.x);
            EXPECT_EQ(actual.y, expected.y);
            EXPECT_EQ(actual.w, expected.w);
            EXPECT_EQ(actual.h, expected.h);
        }

        bool Overlap(const Rect& a, const Rect& b) {
            return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
        }

    }  // namespace

    TEST(SpriteSheetGridTest, SixByTwoTilesTheChessSheetExactly) {
        auto cells = ComputeGridCells(kChessSheetWidth, kChessSheetHeight, Grid(6, 2));
        ASSERT_TRUE(cells.has_value());
        ASSERT_EQ(cells->size(), 12u);

        float widthSum = 0.0f;
        float heightSum = 0.0f;
        for (size_t i = 0; i < 6; ++i) {
            widthSum += (*cells)[i].w;
            EXPECT_GE((*cells)[i].w, 116.0f);
            EXPECT_LE((*cells)[i].w, 117.0f);
        }
        for (size_t row = 0; row < 2; ++row) {
            heightSum += (*cells)[row * 6].h;
        }
        EXPECT_EQ(widthSum, 700.0f);
        EXPECT_EQ(heightSum, 381.0f);

        for (size_t i = 0; i < cells->size(); ++i) {
            for (size_t j = i + 1; j < cells->size(); ++j) {
                EXPECT_FALSE(Overlap((*cells)[i], (*cells)[j])) << i << " and " << j;
            }
        }
        // Neighbours touch: the next cell starts where this one ends
        for (size_t i = 0; i + 1 < 6; ++i) {
            EXPECT_EQ((*cells)[i].x + (*cells)[i].w, (*cells)[i + 1].x);
        }
        EXPECT_EQ((*cells)[5].x + (*cells)[5].w, 700.0f);
        EXPECT_EQ((*cells)[6].y, (*cells)[0].h);
    }

    TEST(SpriteSheetGridTest, CellsAreRowMajorFromTheTopLeft) {
        auto cells = ComputeGridCells(kChessSheetWidth, kChessSheetHeight, Grid(6, 2));
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ((*cells)[0].x, 0.0f);
        EXPECT_EQ((*cells)[0].y, 0.0f);
        EXPECT_EQ((*cells)[1].y, 0.0f);
        EXPECT_GT((*cells)[1].x, 0.0f);
        EXPECT_EQ((*cells)[6].x, 0.0f);
        EXPECT_GT((*cells)[6].y, 0.0f);
    }

    TEST(SpriteSheetGridTest, MarginAndSpacingWorkedByHand) {
        // inner 92, minus one gap of 2 is 90: cells of 45; columns 4..49 and 51..96, the same for the rows
        auto cells = ComputeGridCells(100, 100, Grid(2, 2, 4, 2));
        ASSERT_TRUE(cells.has_value());
        ASSERT_EQ(cells->size(), 4u);
        ExpectRect((*cells)[0], Rect(4.0f, 4.0f, 45.0f, 45.0f));
        ExpectRect((*cells)[1], Rect(51.0f, 4.0f, 45.0f, 45.0f));
        ExpectRect((*cells)[2], Rect(4.0f, 51.0f, 45.0f, 45.0f));
        ExpectRect((*cells)[3], Rect(51.0f, 51.0f, 45.0f, 45.0f));
    }

    TEST(SpriteSheetGridTest, OddRemainderGivesWiderCellsAtMostOnePixelApart) {
        // 10 wide in 3 columns with a gap of 1: 8 left, edges at 0, 2, 5 and 8 of it
        auto cells = ComputeGridCells(10, 4, Grid(3, 1, 0, 1));
        ASSERT_TRUE(cells.has_value());
        ASSERT_EQ(cells->size(), 3u);
        ExpectRect((*cells)[0], Rect(0.0f, 0.0f, 2.0f, 4.0f));
        ExpectRect((*cells)[1], Rect(3.0f, 0.0f, 3.0f, 4.0f));
        ExpectRect((*cells)[2], Rect(7.0f, 0.0f, 3.0f, 4.0f));
    }

    TEST(SpriteSheetGridTest, OneByOneIsTheWholeSheet) {
        auto cells = ComputeGridCells(64, 32, Grid(1, 1));
        ASSERT_TRUE(cells.has_value());
        ASSERT_EQ(cells->size(), 1u);
        ExpectRect((*cells)[0], Rect(0.0f, 0.0f, 64.0f, 32.0f));
    }

    TEST(SpriteSheetGridTest, EveryPixelCellFitsExactly) {
        auto cells = ComputeGridCells(4, 2, Grid(4, 2));
        ASSERT_TRUE(cells.has_value());
        EXPECT_EQ(cells->size(), 8u);
        ExpectRect((*cells)[7], Rect(3.0f, 1.0f, 1.0f, 1.0f));
    }

    TEST(SpriteSheetGridTest, GridErrors) {
        EXPECT_EQ(ComputeGridCells(0, 100, Grid(2, 2)).error(), SheetGridError::NoSheet);
        EXPECT_EQ(ComputeGridCells(100, 0, Grid(2, 2)).error(), SheetGridError::NoSheet);
        EXPECT_EQ(ComputeGridCells(100, 100, Grid(0, 2)).error(), SheetGridError::BadCount);
        EXPECT_EQ(ComputeGridCells(100, 100, Grid(2, -1)).error(), SheetGridError::BadCount);
        EXPECT_EQ(ComputeGridCells(1000, 1000, Grid(kMaxSheetGridAxis + 1, 1)).error(), SheetGridError::BadCount);
        EXPECT_EQ(ComputeGridCells(1000, 1000, Grid(kMaxSheetGridAxis, kMaxSheetGridAxis)).error(), SheetGridError::TooManyCells);
        EXPECT_TRUE(ComputeGridCells(1000, 1000, Grid(32, 32)).has_value());

        // Four columns need 4 px after the margins; 10 - 2 * 4 leaves 2
        EXPECT_EQ(ComputeGridCells(10, 100, Grid(4, 1, 4)).error(), SheetGridError::CellTooSmall);
        // The three gaps of 3 take 9 of 10
        EXPECT_EQ(ComputeGridCells(10, 100, Grid(4, 1, 0, 3)).error(), SheetGridError::CellTooSmall);
        EXPECT_EQ(ComputeGridCells(100, 100, Grid(2, 2, -1)).error(), SheetGridError::CellTooSmall);
        EXPECT_EQ(ComputeGridCells(100, 100, Grid(2, 2, 0, -1)).error(), SheetGridError::CellTooSmall);
        EXPECT_EQ(ComputeGridCells(100, 100, Grid(2, 2, 1000000000, 0)).error(), SheetGridError::CellTooSmall);
    }

    TEST(SpriteSheetGridTest, EveryErrorHasText) {
        for (const SheetGridError error :
             {SheetGridError::NoSheet, SheetGridError::BadCount, SheetGridError::TooManyCells, SheetGridError::CellTooSmall, SheetGridError::DuplicateName, SheetGridError::BadName}) {
            EXPECT_FALSE(SheetGridErrorText(error).empty());
        }
    }

    TEST(SpriteSheetGridTest, ParseSheetNamesSplitsAtWhitespaceAndCommas) {
        const Vector<String> names = ParseSheetNames(" king,queen\n\trook ,, bishop\r\n");
        ASSERT_EQ(names.size(), 4u);
        EXPECT_EQ(names[0], "king");
        EXPECT_EQ(names[1], "queen");
        EXPECT_EQ(names[2], "rook");
        EXPECT_EQ(names[3], "bishop");
        EXPECT_TRUE(ParseSheetNames("").empty());
        EXPECT_TRUE(ParseSheetNames(" , \n").empty());
    }

    TEST(SpriteSheetGridTest, NamesArePrefixAndPaddedIndex) {
        TextureLibrary library;
        const PoolString sheet = PoolString::Intern("assets/sheet.png");
        auto plan = PlanGridSlice(library, sheet, 100, 100, Grid(4, 3), Named("piece"), SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(plan->entries.size(), 12u);
        // The last index is 11, two digits, so the first ones are padded to it
        EXPECT_EQ(plan->entries[0].name.ToStringView(), "piece_00");
        EXPECT_EQ(plan->entries[9].name.ToStringView(), "piece_09");
        EXPECT_EQ(plan->entries[11].name.ToStringView(), "piece_11");
        EXPECT_EQ(plan->entries[0].texturePath, sheet);
        EXPECT_TRUE(plan->replaced.empty());
        EXPECT_EQ(plan->skipped, 0u);
    }

    TEST(SpriteSheetGridTest, StartIndexShiftsTheNumbersAndTheWidth) {
        TextureLibrary library;
        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 100, 100, Grid(2, 1), Named("a", 9), SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(plan->entries.size(), 2u);
        EXPECT_EQ(plan->entries[0].name.ToStringView(), "a_09");
        EXPECT_EQ(plan->entries[1].name.ToStringView(), "a_10");
    }

    TEST(SpriteSheetGridTest, ExplicitNamesWinAndTheRestFallBackToThePrefix) {
        TextureLibrary library;
        SheetGridNaming naming = Named("p");
        naming.names = ParseSheetNames("king queen");
        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 90, 10, Grid(3, 1), naming, SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(plan->entries.size(), 3u);
        EXPECT_EQ(plan->entries[0].name.ToStringView(), "king");
        EXPECT_EQ(plan->entries[1].name.ToStringView(), "queen");
        EXPECT_EQ(plan->entries[2].name.ToStringView(), "p_2");
    }

    TEST(SpriteSheetGridTest, AnEmptyExplicitNameFallsBack) {
        TextureLibrary library;
        SheetGridNaming naming = Named("p");
        naming.names = {String("a"), String(), String("c")};
        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 30, 10, Grid(3, 1), naming, SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(plan->entries[1].name.ToStringView(), "p_1");
    }

    TEST(SpriteSheetGridTest, PlanErrors) {
        TextureLibrary library;
        const PoolString sheet = PoolString::Intern("assets/s.png");

        EXPECT_EQ(PlanGridSlice(library, PoolString{}, 100, 100, Grid(2, 1), Named("p"), SheetConflictPolicy::Overwrite).error(), SheetGridError::NoSheet);
        EXPECT_EQ(PlanGridSlice(library, sheet, 0, 100, Grid(2, 1), Named("p"), SheetConflictPolicy::Overwrite).error(), SheetGridError::NoSheet);
        EXPECT_EQ(PlanGridSlice(library, sheet, 100, 100, Grid(0, 1), Named("p"), SheetConflictPolicy::Overwrite).error(), SheetGridError::BadCount);

        // No prefix and no explicit name for the cell
        EXPECT_EQ(PlanGridSlice(library, sheet, 100, 100, Grid(2, 1), Named(""), SheetConflictPolicy::Overwrite).error(), SheetGridError::BadName);

        SheetGridNaming bad = Named("p");
        bad.names = {String("good"), String("no/slash")};
        EXPECT_EQ(PlanGridSlice(library, sheet, 100, 100, Grid(2, 1), bad, SheetConflictPolicy::Overwrite).error(), SheetGridError::BadName);

        SheetGridNaming twice = Named("p");
        twice.names = {String("same"), String("same")};
        EXPECT_EQ(PlanGridSlice(library, sheet, 100, 100, Grid(2, 1), twice, SheetConflictPolicy::Overwrite).error(), SheetGridError::DuplicateName);

        // An explicit name that meets a generated one
        SheetGridNaming meets = Named("p");
        meets.names = {String(), String("p_0")};
        EXPECT_EQ(PlanGridSlice(library, sheet, 100, 100, Grid(2, 1), meets, SheetConflictPolicy::Overwrite).error(), SheetGridError::DuplicateName);

        SheetGridNaming longName = Named("p");
        longName.prefix = String(kMaxSpriteNameLength + 1, 'x');
        EXPECT_EQ(PlanGridSlice(library, sheet, 100, 100, Grid(2, 1), longName, SheetConflictPolicy::Overwrite).error(), SheetGridError::BadName);
    }

    TEST(SpriteSheetGridTest, OverwriteListsTheReplacedNames) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("p_0", "assets/old.png", Rect(1.0f, 2.0f, 3.0f, 4.0f)));
        library.AddOrUpdate(MakeEntry("other", "assets/old.png", Rect()));

        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 20, 10, Grid(2, 1), Named("p"), SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(plan->entries.size(), 2u);
        ASSERT_EQ(plan->replaced.size(), 1u);
        EXPECT_EQ(plan->replaced[0].ToStringView(), "p_0");
        EXPECT_EQ(plan->skipped, 0u);
    }

    TEST(SpriteSheetGridTest, SkipCountsTheExistingNamesAndLeavesThemOut) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("p_0", "assets/old.png", Rect(1.0f, 2.0f, 3.0f, 4.0f)));

        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 20, 10, Grid(2, 1), Named("p"), SheetConflictPolicy::Skip);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(plan->entries.size(), 1u);
        EXPECT_EQ(plan->entries[0].name.ToStringView(), "p_1");
        EXPECT_TRUE(plan->replaced.empty());
        EXPECT_EQ(plan->skipped, 1u);
    }

    TEST(SpriteSheetGridTest, PlanningLeavesTheLibraryAlone) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("p_0", "assets/old.png", Rect(1.0f, 2.0f, 3.0f, 4.0f)));

        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 20, 10, Grid(2, 1), Named("p"), SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(library.GetAll().size(), 1u);
        EXPECT_EQ(library.GetAll()[0].texturePath.ToStringView(), "assets/old.png");
        ExpectRect(library.GetAll()[0].srcRect, Rect(1.0f, 2.0f, 3.0f, 4.0f));
    }

    TEST(SpriteSheetGridTest, ApplyReplacesInPlaceAndAppendsNewOnesInCellOrder) {
        TextureLibrary library;
        library.AddOrUpdate(MakeEntry("first", "assets/old.png", Rect()));
        library.AddOrUpdate(MakeEntry("p_1", "assets/old.png", Rect(9.0f, 9.0f, 9.0f, 9.0f)));
        library.AddOrUpdate(MakeEntry("last", "assets/old.png", Rect()));

        auto plan = PlanGridSlice(library, PoolString::Intern("assets/s.png"), 30, 10, Grid(3, 1), Named("p"), SheetConflictPolicy::Overwrite);
        ASSERT_TRUE(plan.has_value());
        ApplyGridSlice(library, *plan);

        const auto& all = library.GetAll();
        ASSERT_EQ(all.size(), 5u);
        EXPECT_EQ(all[0].name.ToStringView(), "first");
        EXPECT_EQ(all[1].name.ToStringView(), "p_1");
        EXPECT_EQ(all[1].texturePath.ToStringView(), "assets/s.png");
        ExpectRect(all[1].srcRect, Rect(10.0f, 0.0f, 10.0f, 10.0f));
        EXPECT_EQ(all[2].name.ToStringView(), "last");
        EXPECT_EQ(all[3].name.ToStringView(), "p_0");
        EXPECT_EQ(all[4].name.ToStringView(), "p_2");
    }

}  // namespace BECore::Tests
