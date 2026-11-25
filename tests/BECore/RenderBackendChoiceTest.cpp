#include <BECore/Application/RenderBackendChoice.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(RenderBackendChoiceTest, EachSpellingParsesAndNamesItselfBack) {
        for (const RenderBackendChoice choice : {RenderBackendChoice::Software, RenderBackendChoice::SdlGpu, RenderBackendChoice::Vulkan}) {
            const Optional<RenderBackendChoice> parsed = ParseRenderBackend(RenderBackendName(choice));
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(*parsed, choice);
        }
    }

    TEST(RenderBackendChoiceTest, AnyOtherTextIsNotABackend) {
        for (const char* text : {"", "auto", "Auto", "SDL-GPU", "sdl_gpu", "metal", " vulkan", "vulkan "}) {
            EXPECT_FALSE(ParseRenderBackend(text).has_value()) << text;
        }
    }

    TEST(RenderBackendChoiceTest, AutoHasNoSpelling) {
        EXPECT_TRUE(RenderBackendName(RenderBackendChoice::Auto).empty());
    }

}  // namespace BECore::Tests
