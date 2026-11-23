#include <BECore/Audio/AudioDecode.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/Profiler/Alloc/AllocRouting.h>
#include <BECore/Text/Font.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/TextTestSupport.h>
#include <CoreSDL/ImageFile.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>
#include <gtest/gtest.h>
#include <imgui.h>
#include <pugixml.hpp>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSize = 256;
        constexpr float kGlyphSize = 32.0f;
        constexpr char32_t kGlyphCodePoint = U'B';
        constexpr int kPngSide = 8;

        Vector<uint8_t> ReadFixture(const char* name) {
            const std::filesystem::path path = AudioFixtureFolder() / name;
            return ReadFileBytes(path);
        }

        /// The allocations the name table holds under the name of @p source, summed over the zones
        uint64_t NamedCount(AllocSource source) {
            uint64_t count = 0;
            for (const AllocSiteStats& row : AllocTracker::GetNameStats()) {
                if (row.name != nullptr && std::strcmp(row.name, AllocTracker::GetSourceName(source)) == 0) {
                    count += row.count;
                }
            }
            return count;
        }

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
        const uint64_t namedBefore = NamedCount(AllocSource::Pugixml);
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
        EXPECT_EQ(NamedCount(AllocSource::Pugixml) - namedBefore, during.count - before.count);
    }

    TEST(AllocRoutingTest, AnImGuiFrameIsCountedAsImGui) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::ImGui);
        const uint64_t namedBefore = NamedCount(AllocSource::ImGui);
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
        EXPECT_GT(NamedCount(AllocSource::ImGui), namedBefore);
    }

    TEST(AllocRoutingTest, SdlAllocationsAreCountedAsSdlWithoutInit) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::SDL);
        const uint64_t namedBefore = NamedCount(AllocSource::SDL);
        void* block = SDL_malloc(kSize);
        char* text = SDL_strdup("routed");
        const AllocSourceTotals during = AllocTracker::GetSourceTotals(AllocSource::SDL);
        SDL_free(text);
        SDL_free(block);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::SDL);

        EXPECT_GE(during.count, before.count + 2);
        EXPECT_GE(during.bytes, before.bytes + kSize);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_EQ(NamedCount(AllocSource::SDL) - namedBefore, after.count - before.count);

        // SDL keeps some of what a surface needs (pixel format tables) for the process: only the count is checked
        SDL_Surface* surface = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
        EXPECT_GT(AllocTracker::GetSourceTotals(AllocSource::SDL).count, after.count);
        SDL_DestroySurface(surface);
    }

    // ---- The include sites of stb and dr_libs (a profile build) ----

    // Font::RasterizeGlyph runs stb_truetype on scratch memory (an stb allocation hook), so the heap sees none of its allocations: the png case
    // below is the one that proves the Stb routing itself
    TEST(AllocRoutingTest, ARasterizedGlyphMakesNoStbHeapAllocation) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        EngineEnvironment::EnsureCoreServices();
        auto font = LoadTestFont();
        ASSERT_TRUE(font);

        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::Stb);
        const GlyphBitmap bitmap = font->RasterizeGlyph(font->FindGlyph(kGlyphCodePoint), kGlyphSize);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::Stb);

        EXPECT_FALSE(bitmap.coverage.empty());
        EXPECT_EQ(after.count, before.count);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
    }

    TEST(AllocRoutingTest, APngDecodeIsCountedAsStbAndLeavesNothingBehind) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const std::filesystem::path path = TestTempDirectory("AllocRouting").concat(".png");
        {
            SDL_Surface* surface = SDL_CreateSurface(kPngSide, kPngSide, SDL_PIXELFORMAT_RGBA32);
            ASSERT_NE(surface, nullptr);
            const auto utf8 = path.u8string();  //-NoStd: SDL_image takes the path as UTF-8 chars
            const bool saved = IMG_SavePNG(surface, reinterpret_cast<const char*>(utf8.c_str()));
            SDL_DestroySurface(surface);
            ASSERT_TRUE(saved);
        }

        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::Stb);
        const uint64_t namedBefore = NamedCount(AllocSource::Stb);
        const auto image = ReadImageFile(path);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::Stb);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        ASSERT_TRUE(image.has_value());
        EXPECT_GT(after.count, before.count);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_EQ(after.frees - before.frees, after.count - before.count);
        EXPECT_EQ(NamedCount(AllocSource::Stb) - namedBefore, after.count - before.count);
    }

    TEST(AllocRoutingTest, AnOggStreamIsCountedAsStbAndFreesItsDecoder) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const Vector<uint8_t> bytes = ReadFixture("stereo-tone.ogg");
        ASSERT_FALSE(bytes.empty());

        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::Stb);
        const uint64_t namedBefore = NamedCount(AllocSource::Stb);
        // DecodeOgg opens its decoder on scratch memory and so allocates nothing in stb_vorbis; the stream's decoder is the heap one
        {
            const auto stream = OpenAudioStream(bytes);
            ASSERT_TRUE(stream.has_value());
        }
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::Stb);

        EXPECT_GT(after.count, before.count);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_EQ(after.frees - before.frees, after.count - before.count);
        EXPECT_EQ(NamedCount(AllocSource::Stb) - namedBefore, after.count - before.count);
    }

    TEST(AllocRoutingTest, AFlacDecodeIsCountedAsDrLibsAndLeavesNothingBehind) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const Vector<uint8_t> bytes = ReadFixture("stereo-tone.flac");
        ASSERT_FALSE(bytes.empty());

        const AllocSourceTotals before = AllocTracker::GetSourceTotals(AllocSource::DrLibs);
        const uint64_t namedBefore = NamedCount(AllocSource::DrLibs);
        const auto decoded = DecodeFlac(bytes);
        const AllocSourceTotals after = AllocTracker::GetSourceTotals(AllocSource::DrLibs);

        ASSERT_TRUE(decoded.has_value());
        EXPECT_GT(after.count, before.count);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_EQ(after.frees - before.frees, after.count - before.count);
        EXPECT_EQ(NamedCount(AllocSource::DrLibs) - namedBefore, after.count - before.count);
    }

}  // namespace BECore::Tests
