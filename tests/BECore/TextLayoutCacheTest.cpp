#include <BECore/Text/Font.h>
#include <BECore/Text/TextLayoutCache.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/TextTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {
        constexpr const char* kBoldFontPath = "assets/fonts/PT_Sans-Web-Bold.ttf";
    }  // namespace

    class TextLayoutCacheTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            font = LoadTestFont();
            ASSERT_TRUE(font);
            options.fontSize = 20.0f;
            options.maxWidth = 160.0f;
        }

        IntrusivePtr<Font> font;
        TextLayoutOptions options;
        TextLayoutCache cache;
    };

    TEST_F(TextLayoutCacheTest, AnUnchangedKeyIsLaidOutOnce) {
        cache.Get(*font, "Score: 10", options);
        cache.Get(*font, "Score: 10", options);

        EXPECT_EQ(cache.GetLayoutCount(), 1u);
    }

    TEST_F(TextLayoutCacheTest, AChangedTextOptionOrFontIsLaidOutAgain) {
        cache.Get(*font, "Score: 10", options);
        cache.Get(*font, "Score: 11", options);
        EXPECT_EQ(cache.GetLayoutCount(), 2u);

        options.maxWidth = 80.0f;
        cache.Get(*font, "Score: 11", options);
        EXPECT_EQ(cache.GetLayoutCount(), 3u);

        auto other = LoadTestFont(kBoldFontPath);
        ASSERT_TRUE(other);
        cache.Get(*other, "Score: 11", options);
        EXPECT_EQ(cache.GetLayoutCount(), 4u);
    }

    TEST_F(TextLayoutCacheTest, ResetMakesTheNextGetLayOutAgainWithTheSameResult) {
        const TextLayoutResult first = cache.Get(*font, "Score: 10 and more", options);

        cache.Reset();
        EXPECT_TRUE(cache.GetLast().glyphs.empty());
        const TextLayoutResult& again = cache.Get(*font, "Score: 10 and more", options);

        EXPECT_EQ(cache.GetLayoutCount(), 2u);
        ASSERT_EQ(again.glyphs.size(), first.glyphs.size());
        for (size_t i = 0; i < first.glyphs.size(); ++i) {
            EXPECT_EQ(again.glyphs[i].glyph, first.glyphs[i].glyph);
            EXPECT_FLOAT_EQ(again.glyphs[i].x, first.glyphs[i].x);
        }
        EXPECT_FLOAT_EQ(again.width, first.width);
    }

    TEST_F(TextLayoutCacheTest, AChangedTextAllocatesNothingAfterTheLongestOneWasLaidOut) {
        const StringView texts[] = {"Score: 123456 and a long caption", "Score: 7", "Score: 4242 and a caption", "Score: 99"};
        cache.Get(*font, texts[0], options);

        ExpectNoAllocations([&] {
            for (int round = 0; round < 10; ++round) {
                for (const StringView text : texts) {
                    cache.Get(*font, text, options);
                }
            }
        });
    }

}  // namespace BECore::Tests
