#include <BECore/Profiler/Alloc/AllocRouting.h>
#include <Common/AllocCheck.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <cstring>
#include <gtest/gtest.h>
#include <imgui.h>
#include <pugixml.hpp>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSize = 256;

    }  // namespace

    TEST(AllocRoutingTest, MallocGivesWritableMemory) {
        auto* bytes = static_cast<unsigned char*>(AllocRouting::Malloc(AllocSource::Stb, kSize));
        ASSERT_NE(bytes, nullptr);
        std::memset(bytes, 0xAB, kSize);
        EXPECT_EQ(bytes[kSize - 1], 0xAB);
        AllocRouting::Free(AllocSource::Stb, bytes);
    }

    TEST(AllocRoutingTest, CallocGivesZeroedMemory) {
        auto* bytes = static_cast<unsigned char*>(AllocRouting::Calloc(AllocSource::DrLibs, kSize, 4));
        ASSERT_NE(bytes, nullptr);
        for (size_t i = 0; i < kSize * 4; ++i) {
            ASSERT_EQ(bytes[i], 0) << "byte " << i;
        }
        AllocRouting::Free(AllocSource::DrLibs, bytes);
    }

    TEST(AllocRoutingTest, ReallocKeepsTheContentWhenTheBlockGrows) {
        auto* bytes = static_cast<unsigned char*>(AllocRouting::Malloc(AllocSource::Stb, kSize));
        ASSERT_NE(bytes, nullptr);
        std::memset(bytes, 0x5C, kSize);
        bytes = static_cast<unsigned char*>(AllocRouting::Realloc(AllocSource::Stb, bytes, kSize * 64));
        ASSERT_NE(bytes, nullptr);
        EXPECT_EQ(bytes[0], 0x5C);
        EXPECT_EQ(bytes[kSize - 1], 0x5C);
        AllocRouting::Free(AllocSource::Stb, bytes);
    }

    TEST(AllocRoutingTest, ReallocOfNullptrAndFreeOfNullptrAreAccepted) {
        void* bytes = AllocRouting::Realloc(AllocSource::Stb, nullptr, kSize);
        ASSERT_NE(bytes, nullptr);
        AllocRouting::Free(AllocSource::Stb, bytes);
        AllocRouting::Free(AllocSource::Stb, nullptr);
    }

    // ---- The libraries behind AllocRouting::Install (a profile build) ----

    TEST(AllocRoutingTest, TheLibrariesAreRoutedBeforeMain) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EXPECT_TRUE(AllocRouting::IsInstalled());
    }

    TEST(AllocRoutingTest, ParsingAnXmlDocumentIsCountedAsPugixmlAndItsDestructionGivesTheBytesBack) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
        AllocSourceTotals during;
        {
            pugi::xml_document document;
            ASSERT_TRUE(document.load_string("<root><child a=\"1\"/><child a=\"2\"/></root>"));
            during = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
        }
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::Pugixml);
        EXPECT_GT(during.count, before.count);
        EXPECT_GT(during.liveBytes, before.liveBytes);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_EQ(after.frees - before.frees, during.count - before.count);
    }

    TEST(AllocRoutingTest, AnImGuiFrameIsCountedAsImGui) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::ImGui);
        ImGuiContext* context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.Fonts->Build();
        ImGui::NewFrame();
        ImGui::Render();
        const AllocSourceTotals during = AllocTracker::GetSourceTotals(AllocSource::ImGui);
        ImGui::DestroyContext(context);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::ImGui);
        EXPECT_GT(during.count, before.count);
        EXPECT_GT(during.bytes, before.bytes);
        EXPECT_LT(after.liveBytes, during.liveBytes);
    }

    TEST(AllocRoutingTest, SdlAllocationsAreCountedAsSdlWithoutInit) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::SDL);
        void* block = SDL_malloc(kSize);
        char* text = SDL_strdup("routed");
        const AllocSourceTotals during = AllocTracker::GetSourceTotals(AllocSource::SDL);
        SDL_free(text);
        SDL_free(block);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::SDL);

        EXPECT_GE(during.count, before.count + 2);
        EXPECT_GE(during.bytes, before.bytes + kSize);
        EXPECT_EQ(after.liveBytes, before.liveBytes);

        // SDL keeps some of what a surface needs (pixel format tables) for the process: only the count is checked
        SDL_Surface* surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
        EXPECT_GT(AllocTracker::GetSourceTotals(AllocSource::SDL).count, after.count);
        SDL_DestroySurface(surface);
    }

}  // namespace BECore::Tests
