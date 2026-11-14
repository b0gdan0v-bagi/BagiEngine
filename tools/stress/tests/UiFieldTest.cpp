#include <BECore/Text/Utf8.h>
#include <Stress/UiField.h>
#include <gtest/gtest.h>

/// Coverage of UiField: the grid and the text of a label are pure functions of the index and the count, and the grid always fits the canvas.
namespace BECore::Stress::Tests {

    namespace {

        const FieldView kView{1920.0f, 1080.0f};
        constexpr uint32_t kCount = 2000;
        constexpr float kEpsilon = 0.01f;

        bool IsValidUtf8(StringView text) {
            size_t position = 0;
            while (position < text.size()) {
                if (Utf8::DecodeNext(text, position) == U'�') {
                    return false;
                }
            }
            return true;
        }

        bool HasCyrillic(StringView text) {
            size_t position = 0;
            while (position < text.size()) {
                const char32_t codePoint = Utf8::DecodeNext(text, position);
                if (codePoint >= U'Ѐ' && codePoint <= U'ӿ') {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(UiFieldTest, TheSameIndexGivesTheSameCellAndText) {
        const UiGrid grid = MakeGrid(kCount, kView);
        for (uint32_t i : {0u, 1u, 77u, 1999u}) {
            const UiCell a = CellOf(i, grid);
            const UiCell b = CellOf(i, MakeGrid(kCount, kView));
            EXPECT_EQ(a.x, b.x);
            EXPECT_EQ(a.y, b.y);
            EXPECT_EQ(a.fontSize, b.fontSize);
            EXPECT_EQ(TextOf(i), TextOf(i));
        }
    }

    TEST(UiFieldTest, EveryCellIsInsideTheViewForEveryCount) {
        for (uint32_t count : {1u, 2u, 7u, 300u, 20000u}) {
            const UiGrid grid = MakeGrid(count, kView);
            EXPECT_GE(grid.columns * grid.rows, count) << count;
            EXPECT_LT((grid.rows - 1) * grid.columns, count) << "no empty row at the end: " << count;
            for (uint32_t i = 0; i < count; ++i) {
                const UiCell cell = CellOf(i, grid);
                ASSERT_GE(cell.x, 0.0f) << count << " " << i;
                ASSERT_GE(cell.y, 0.0f) << count << " " << i;
                ASSERT_LE(cell.x + cell.width, kView.width + kEpsilon) << count << " " << i;
                ASSERT_LE(cell.y + cell.height, kView.height + kEpsilon) << count << " " << i;
                ASSERT_GT(cell.width, 0.0f);
                ASSERT_GT(cell.height, 0.0f);
            }
        }
    }

    TEST(UiFieldTest, CountZeroIsOneEmptyCellOfTheWholeView) {
        const UiGrid grid = MakeGrid(0, kView);
        EXPECT_EQ(grid.columns, 1u);
        EXPECT_EQ(grid.rows, 1u);
        EXPECT_EQ(grid.cellWidth, kView.width);
        EXPECT_EQ(grid.cellHeight, kView.height);
    }

    TEST(UiFieldTest, TheCellsAreWiderThanHighForALabelOfWords) {
        const UiGrid grid = MakeGrid(300, kView);
        EXPECT_GT(grid.cellWidth, 2.0f * grid.cellHeight);
    }

    TEST(UiFieldTest, TheFontSizeFollowsTheRowWithinItsRange) {
        const UiGrid grid = MakeGrid(kCount, kView);
        float smallest = 1000.0f;
        float largest = 0.0f;
        for (uint32_t i = 0; i < kCount; ++i) {
            const UiCell cell = CellOf(i, grid);
            smallest = std::min(smallest, cell.fontSize);
            largest = std::max(largest, cell.fontSize);
            EXPECT_EQ(cell.fontSize, CellOf(i - i % grid.columns, grid).fontSize) << "one size per row";
        }
        EXPECT_EQ(smallest, kUiMinFontSize);
        EXPECT_EQ(largest, kUiMinFontSize + kUiFontStep * static_cast<float>(kUiFontSizes - 1));
    }

    TEST(UiFieldTest, EveryTextIsNotEmptyAndValidUtf8) {
        for (uint32_t i = 0; i < kCount; ++i) {
            const StringView text = TextOf(i);
            ASSERT_FALSE(text.empty()) << i;
            ASSERT_TRUE(IsValidUtf8(text)) << i;
        }
    }

    TEST(UiFieldTest, EnglishAndRussianBothOccurAndFollowTheKind) {
        size_t english = 0;
        size_t russian = 0;
        for (uint32_t i = 0; i < kCount; ++i) {
            const bool cyrillic = HasCyrillic(TextOf(i));
            switch (KindOf(i)) {
                case UiLabelKind::English:
                    EXPECT_FALSE(cyrillic) << i;
                    EXPECT_EQ(i % 2, 0u) << "English is the even labels";
                    ++english;
                    break;
                case UiLabelKind::Russian:
                    EXPECT_TRUE(cyrillic) << i;
                    EXPECT_EQ(i % 2, 1u) << "Russian is the odd labels";
                    ++russian;
                    break;
                case UiLabelKind::Key:
                    EXPECT_EQ(i % kUiKeyEvery, 0u);
                    break;
                case UiLabelKind::Variable:
                    break;
            }
        }
        EXPECT_GT(english, 0u);
        EXPECT_GT(russian, 0u);
    }

    TEST(UiFieldTest, EveryEighthLabelIsAKeyAndTheKeysCycleThroughThePool) {
        Vector<StringView> keys;
        for (uint32_t i = 0; i < kCount; ++i) {
            if (i % kUiKeyEvery != 0) {
                EXPECT_NE(KindOf(i), UiLabelKind::Key) << i;
                continue;
            }
            ASSERT_EQ(KindOf(i), UiLabelKind::Key) << i;
            const String expected = String("{#") + String(KeyOf(i).data(), KeyOf(i).size()) + "}";
            EXPECT_EQ(TextOf(i), StringView(expected));
            if (eastl::find(keys.begin(), keys.end(), KeyOf(i)) == keys.end()) {
                keys.push_back(KeyOf(i));
            }
        }
        EXPECT_EQ(keys.size(), size_t{kUiPoolSize});
    }

    TEST(UiFieldTest, SomeLabelsHoldAVariableAndTheRestHoldNone) {
        size_t variables = 0;
        for (uint32_t i = 0; i < kCount; ++i) {
            const bool holds = TextOf(i).find('{') != StringView::npos;
            if (KindOf(i) == UiLabelKind::Variable) {
                EXPECT_TRUE(holds) << i;
                EXPECT_EQ(TextOf(i).find("{#"), StringView::npos) << i;
                ++variables;
            } else if (KindOf(i) == UiLabelKind::Key) {
                EXPECT_TRUE(holds) << i;
            } else {
                EXPECT_FALSE(holds) << i;
            }
        }
        EXPECT_GT(variables, 0u);
    }

    TEST(UiFieldTest, TheLiteralTextsComeFromPoolsOfTheNamedSize) {
        Vector<StringView> english;
        Vector<StringView> russian;
        for (uint32_t i = 0; i < 20000; ++i) {
            auto& pool = KindOf(i) == UiLabelKind::English ? english : russian;
            if (KindOf(i) != UiLabelKind::English && KindOf(i) != UiLabelKind::Russian) {
                continue;
            }
            if (eastl::find(pool.begin(), pool.end(), TextOf(i)) == pool.end()) {
                pool.push_back(TextOf(i));
            }
        }
        EXPECT_LE(english.size(), size_t{kUiPoolSize});
        EXPECT_LE(russian.size(), size_t{kUiPoolSize});
        EXPECT_GT(english.size(), size_t{kUiPoolSize} / 2) << "the pool is used, not one sentence";
        EXPECT_GT(russian.size(), size_t{kUiPoolSize} / 2);
    }

}  // namespace BECore::Stress::Tests
