#include <BECore/Text/Utf8.h>
#include <Stress/RmlUi/RmlUiField.h>
#include <Stress/UiField.h>
#include <gtest/gtest.h>

namespace BECore::Stress::Tests {

    namespace {

        const FieldView kView{1920.0f, 1080.0f};

        size_t CountOf(const String& text, StringView part) {
            size_t found = 0;
            for (size_t at = text.find(part.data(), 0, part.size()); at != String::npos; at = text.find(part.data(), at + part.size(), part.size())) {
                ++found;
            }
            return found;
        }

        bool HasCyrillic(const String& text) {
            const StringView view(text.data(), text.size());
            size_t position = 0;
            while (position < view.size()) {
                const char32_t codePoint = Utf8::DecodeNext(view, position);
                if (codePoint >= U'Ѐ' && codePoint <= U'ӿ') {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TEST(RmlUiFieldTest, TheSameArgumentsGiveTheSameMarkup) {
        for (const uint32_t index : {0u, 1u, 7u, 49u}) {
            EXPECT_EQ(MakeDocumentMarkup(index, 50, StressLanguage::En, kView), MakeDocumentMarkup(index, 50, StressLanguage::En, kView));
        }
    }

    TEST(RmlUiFieldTest, TheLinesOfADocumentHaveDistinctSizes) {
        for (uint32_t first = 0; first < kRmlUiSizes; ++first) {
            for (uint32_t second = first + 1; second < kRmlUiSizes; ++second) {
                EXPECT_NE(kRmlUiFontSizes[first], kRmlUiFontSizes[second]);
            }
        }
        const String markup = MakeDocumentMarkup(3, 50, StressLanguage::En, kView);
        for (const uint32_t size : kRmlUiFontSizes) {
            EXPECT_EQ(CountOf(markup, Format("font-size: {}dp;", size)), 1u) << size;
        }
    }

    TEST(RmlUiFieldTest, TheBlockOfEveryDocumentIsInsideTheView) {
        for (const uint32_t count : {1u, 50u, 600u}) {
            const UiGrid grid = MakeGrid(count, kView);
            for (uint32_t index = 0; index < count; ++index) {
                const UiCell cell = CellOf(index, grid);
                EXPECT_GE(cell.x, 0.0f);
                EXPECT_GE(cell.y, 0.0f);
                EXPECT_LE(cell.x + cell.width, kView.width + 0.01f);
                EXPECT_LE(cell.y + cell.height, kView.height + 0.01f);
                // the block of the markup is the cell: its text carries the cell's left edge
                if (index % 97 == 0) {
                    EXPECT_NE(MakeDocumentMarkup(index, count, StressLanguage::En, kView).find(Format("left: {}dp;", cell.x).c_str()), String::npos) << index << " of " << count;
                }
            }
        }
    }

    TEST(RmlUiFieldTest, TheLanguageAndTheIndexParityPickEnglishOrRussian) {
        EXPECT_FALSE(HasCyrillic(MakeDocumentMarkup(0, 50, StressLanguage::En, kView)));
        EXPECT_TRUE(HasCyrillic(MakeDocumentMarkup(1, 50, StressLanguage::En, kView)));
        EXPECT_TRUE(HasCyrillic(MakeDocumentMarkup(0, 50, StressLanguage::Ru, kView)));
        EXPECT_FALSE(HasCyrillic(MakeDocumentMarkup(1, 50, StressLanguage::Ru, kView)));
        EXPECT_NE(MakeDocumentMarkup(0, 50, StressLanguage::En, kView), MakeDocumentMarkup(0, 50, StressLanguage::Ru, kView));
    }

    TEST(RmlUiFieldTest, TheMarkupHasNoKeyNoVariableNoImageAndLetsThePointerThrough) {
        for (uint32_t index = 0; index < 16; ++index) {
            for (const StressLanguage language : {StressLanguage::En, StressLanguage::Ru}) {
                const String markup = MakeDocumentMarkup(index, 16, language, kView);
                EXPECT_EQ(markup.find("{#"), String::npos) << index;
                EXPECT_EQ(markup.find("{count}"), String::npos) << index;
                EXPECT_EQ(markup.find("{last}"), String::npos) << index;
                EXPECT_EQ(markup.find("<img"), String::npos) << index;
                EXPECT_NE(markup.find("pointer-events: none"), String::npos) << index;
            }
        }
    }

    TEST(RmlUiFieldTest, TheTagsAreBalanced) {
        for (const uint32_t count : {1u, 50u, 600u}) {
            const String markup = MakeDocumentMarkup(count - 1, count, StressLanguage::Ru, kView);
            EXPECT_EQ(CountOf(markup, "<div"), CountOf(markup, "</div>")) << count;
            EXPECT_EQ(CountOf(markup, "<div"), 1u + kRmlUiSizes) << count;
            EXPECT_EQ(CountOf(markup, "<rml>"), 1u);
            EXPECT_EQ(CountOf(markup, "</rml>"), 1u);
            EXPECT_EQ(CountOf(markup, "<body>"), CountOf(markup, "</body>"));
        }
    }

}  // namespace BECore::Stress::Tests
