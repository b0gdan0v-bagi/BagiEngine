#include <CoreSDL/SDLUtils.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// What the shipped config/SDLWindowConfig.xml asks for
        constexpr SDL_WindowFlags kShippedFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY;

        bool Has(SDL_WindowFlags flags, SDL_WindowFlags flag) {
            return (flags & flag) != 0;
        }

    }  // namespace

    TEST(SDLWindowFlagsTest, AutoLeavesTheFlagsOfTheConfig) {
        EXPECT_EQ(SDLUtils::ApplyStartOptions(kShippedFlags, {}), kShippedFlags);
        EXPECT_EQ(SDLUtils::ApplyStartOptions(SDL_WINDOW_RESIZABLE, {}), SDL_WINDOW_RESIZABLE);
    }

    TEST(SDLWindowFlagsTest, HeadlessTakesVulkanOutAndNothingElse) {
        const SDL_WindowFlags flags = SDLUtils::ApplyStartOptions(kShippedFlags, {.headless = true});
        EXPECT_FALSE(Has(flags, SDL_WINDOW_VULKAN));
        EXPECT_EQ(flags, kShippedFlags & ~SDL_WINDOW_VULKAN);
    }

    TEST(SDLWindowFlagsTest, HiddenHidesAndFixesTheOutputSize) {
        const SDL_WindowFlags flags = SDLUtils::ApplyStartOptions(kShippedFlags, {.hidden = true});
        EXPECT_TRUE(Has(flags, SDL_WINDOW_HIDDEN));
        EXPECT_FALSE(Has(flags, SDL_WINDOW_MAXIMIZED));
        EXPECT_FALSE(Has(flags, SDL_WINDOW_HIGH_PIXEL_DENSITY));
        EXPECT_TRUE(Has(flags, SDL_WINDOW_RESIZABLE));
        EXPECT_TRUE(Has(flags, SDL_WINDOW_VULKAN));
    }

    TEST(SDLWindowFlagsTest, TheSdlGpuBackendDropsVulkanAndTheVulkanBackendAddsIt) {
        EXPECT_FALSE(Has(SDLUtils::ApplyStartOptions(kShippedFlags, {.backend = RenderBackendChoice::SdlGpu}), SDL_WINDOW_VULKAN));
        EXPECT_TRUE(Has(SDLUtils::ApplyStartOptions(SDL_WINDOW_RESIZABLE, {.backend = RenderBackendChoice::Vulkan}), SDL_WINDOW_VULKAN));
        EXPECT_EQ(SDLUtils::ApplyStartOptions(kShippedFlags, {.backend = RenderBackendChoice::Vulkan}), kShippedFlags);
    }

    TEST(SDLWindowFlagsTest, TheSoftwareBackendLeavesTheFlagsOfTheConfig) {
        EXPECT_EQ(SDLUtils::ApplyStartOptions(kShippedFlags, {.backend = RenderBackendChoice::Software}), kShippedFlags);
    }

    TEST(SDLWindowFlagsTest, HeadlessWinsOverTheVulkanBackend) {
        EXPECT_FALSE(Has(SDLUtils::ApplyStartOptions(kShippedFlags, {.headless = true, .backend = RenderBackendChoice::Vulkan}), SDL_WINDOW_VULKAN));
    }

    TEST(SDLWindowFlagsTest, HiddenWithAGpuBackendCombines) {
        const SDL_WindowFlags flags = SDLUtils::ApplyStartOptions(kShippedFlags, {.hidden = true, .backend = RenderBackendChoice::SdlGpu});
        EXPECT_EQ(flags, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    }

}  // namespace BECore::Tests
