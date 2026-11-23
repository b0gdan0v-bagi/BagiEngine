// The heap cost of building a glyph atlas and the proof that its pictures do not depend on how it is built: the pixel of every glyph in its page is the one Font and
// GlyphOutline give, and a benchmark run by hand.

#include <BECore/FontAtlasTestSupport.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/Text/FontAtlas.h>
#include <BECore/Text/GlyphOutline.h>
#include <Common/AllocCheck.h>
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kPageSize = 512;
        constexpr int kSmallSize = 24;
        constexpr int kBigSize = 48;
        constexpr int kOutlineRadius = 2;
        constexpr int kRepetitions = 5;
        constexpr uint32_t kBytesPerPixel = 4;
        /// The heap blocks one page of the recording renderer makes beyond its pixel buffer: the texture object and its copy of the pixels, and the atlas's list of pages growing
        constexpr uint64_t kBlocksPerPage = 4;
        /// What a build makes besides its glyphs and pages: the glyph table's bucket array after the one `reserve`, and a rehash
        constexpr uint64_t kBlocksSlack = 16;
        constexpr uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        const RecordedTexture& Recorded(const FontAtlas& atlas, size_t page) {
            return *static_cast<const RecordedTexture*>(atlas.GetPage(static_cast<int32_t>(page)));
        }

        uint8_t AlphaAt(const RecordedTexture& page, uint32_t x, uint32_t y) {
            const auto width = static_cast<uint32_t>(page.GetWidth());
            return page.GetPixels()[(static_cast<size_t>(y) * width + x) * kBytesPerPixel + 3];
        }

        /// FNV-1a over the pixels of every page of @p atlas, so two builds are compared by one number
        uint64_t HashPages(const FontAtlas& atlas) {
            uint64_t hash = kFnvOffset;
            for (size_t page = 0; page < atlas.GetPageCount(); ++page) {
                for (const uint8_t byte : Recorded(atlas, page).GetPixels()) {
                    hash = (hash ^ byte) * kFnvPrime;
                }
            }
            return hash;
        }

        /// The glyphs of the preload ranges that the font has, each once
        Vector<uint32_t> PreloadGlyphs(const Font& font) {
            Vector<uint32_t> glyphs;
            for (const GlyphRange& range : TextSettings::Default().preload) {
                for (char32_t codePoint = range.first; codePoint <= range.last; ++codePoint) {
                    if (const uint32_t glyph = font.FindGlyph(codePoint); glyph != 0) {
                        glyphs.push_back(glyph);
                    }
                }
            }
            std::sort(glyphs.begin(), glyphs.end());
            glyphs.erase(std::unique(glyphs.begin(), glyphs.end()), glyphs.end());
            return glyphs;
        }

        /// What an atlas must hold for @p glyph: the glyph as the font draws it, grown for an outline atlas
        GlyphBitmap Expected(const Font& font, uint32_t glyph, int pixelSize, int outlinePixels) {
            GlyphBitmap bitmap = font.RasterizeGlyph(glyph, static_cast<float>(pixelSize));
            return outlinePixels > 0 ? GlyphOutline::Dilate(bitmap, outlinePixels) : bitmap;
        }

        /// Fails the case for every glyph whose page does not show the expected coverage at the reported rect, or whose 1 px border is not transparent
        void ExpectPicturesMatchTheFont(const FontAtlas& atlas, Span<const uint32_t> glyphs) {
            const Font& font = atlas.GetFont();
            for (const uint32_t glyph : glyphs) {
                const GlyphBitmap expected = Expected(font, glyph, atlas.GetPixelSize(), atlas.GetOutlinePixels());
                const AtlasGlyph* found = atlas.Find(glyph);
                ASSERT_NE(found, nullptr) << "glyph " << glyph;
                if (expected.width == 0 || expected.height == 0) {
                    EXPECT_EQ(found->page, -1) << "glyph " << glyph;
                    continue;
                }
                ASSERT_GE(found->page, 0) << "glyph " << glyph;
                ASSERT_LT(static_cast<size_t>(found->page), atlas.GetPageCount());
                EXPECT_EQ(found->offsetX, static_cast<float>(expected.offsetX)) << "glyph " << glyph;
                EXPECT_EQ(found->offsetY, static_cast<float>(expected.offsetY)) << "glyph " << glyph;
                ASSERT_EQ(found->source.w, static_cast<float>(expected.width)) << "glyph " << glyph;
                ASSERT_EQ(found->source.h, static_cast<float>(expected.height)) << "glyph " << glyph;

                const RecordedTexture& page = Recorded(atlas, static_cast<size_t>(found->page));
                const auto left = static_cast<uint32_t>(found->source.x);
                const auto top = static_cast<uint32_t>(found->source.y);
                ASSERT_GE(left, FontAtlas::kPadding) << "glyph " << glyph;
                ASSERT_GE(top, FontAtlas::kPadding) << "glyph " << glyph;
                ASSERT_LE(left + expected.width + FontAtlas::kPadding, static_cast<uint32_t>(page.GetWidth())) << "glyph " << glyph;
                ASSERT_LE(top + expected.height + FontAtlas::kPadding, static_cast<uint32_t>(page.GetHeight())) << "glyph " << glyph;
                for (uint32_t row = 0; row < expected.height; ++row) {
                    for (uint32_t column = 0; column < expected.width; ++column) {
                        ASSERT_EQ(AlphaAt(page, left + column, top + row), expected.coverage[static_cast<size_t>(row) * expected.width + column])
                            << "glyph " << glyph << " at " << column << "," << row;
                    }
                }
                for (uint32_t column = left - FontAtlas::kPadding; column < left + expected.width + FontAtlas::kPadding; ++column) {
                    EXPECT_EQ(AlphaAt(page, column, top - FontAtlas::kPadding), 0) << "glyph " << glyph << " border above";
                    EXPECT_EQ(AlphaAt(page, column, top + expected.height), 0) << "glyph " << glyph << " border below";
                }
                for (uint32_t row = top; row < top + expected.height; ++row) {
                    EXPECT_EQ(AlphaAt(page, left - FontAtlas::kPadding, row), 0) << "glyph " << glyph << " border left";
                    EXPECT_EQ(AlphaAt(page, left + expected.width, row), 0) << "glyph " << glyph << " border right";
                }
            }
        }

        struct BuildCounts {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            uint64_t secondAllocs = 0;
            double ms = 0.0;
            size_t pages = 0;
            size_t glyphs = 0;
            uint64_t hash = 0;
        };

        /// Builds the atlas of (@p size, @p outline) in the library, which preloads a plain one, and prepares the preload ranges of an outline one
        /// by hand (the library leaves it empty); then asks for it a second time
        BuildCounts MeasureBuild(FontAtlasLibrary& library, RecordingRenderer& renderer, const IntrusivePtr<Font>& font, int size, int outline) {
            library.Clear();
            BuildCounts counts;
            IntrusivePtr<FontAtlas> atlas;
            const auto start = std::chrono::steady_clock::now();
            const AllocCount first = CountAllocations([&] {
                atlas = library.Get(renderer, font, size, outline);
                if (outline > 0) {
                    atlas->PrepareCodePoints(renderer, library.GetSettings().preload);
                }
            });
            const auto stop = std::chrono::steady_clock::now();
            const AllocCount second = CountAllocations([&] { atlas = library.Get(renderer, font, size, outline); });
            counts.allocs = first.count;
            counts.bytes = first.bytes;
            counts.secondAllocs = second.count;
            counts.ms = std::chrono::duration<double, std::milli>(stop - start).count();
            counts.pages = atlas->GetPageCount();
            counts.glyphs = atlas->GetGlyphCount();
            counts.hash = HashPages(*atlas);
            return counts;
        }

        BuildCounts MedianBuild(FontAtlasLibrary& library, RecordingRenderer& renderer, const IntrusivePtr<Font>& font, int size, int outline) {
            Vector<BuildCounts> runs;
            for (int i = 0; i < kRepetitions; ++i) {
                runs.push_back(MeasureBuild(library, renderer, font, size, outline));
            }
            std::sort(runs.begin(), runs.end(), [](const BuildCounts& a, const BuildCounts& b) { return a.ms < b.ms; });
            return runs[runs.size() / 2];
        }

        void Print(const char* name, const BuildCounts& counts) {
            std::cout << name << " glyphs=" << counts.glyphs << " pages=" << counts.pages << " allocs=" << counts.allocs << " bytes=" << counts.bytes << " secondAllocs=" << counts.secondAllocs
                      << " ms=" << counts.ms << " hash=" << counts.hash << "\n";
        }

    }  // namespace

    using FontAtlasScratchTest = FontAtlasLibraryTest;
    using FontAtlasScratchBenchTest = FontAtlasLibraryTest;

    TEST_F(FontAtlasScratchTest, EveryGlyphOfAPlainAtlasIsThePictureOfTheFont) {
        const Vector<uint32_t> glyphs = PreloadGlyphs(*font);
        const IntrusivePtr<FontAtlas> atlas = New<FontAtlas>(font, kBigSize, kPageSize);

        EXPECT_TRUE(atlas->Prepare(*renderer, glyphs));

        EXPECT_GT(atlas->GetPageCount(), 1u) << "the case is meant to cover several pages";
        ExpectPicturesMatchTheFont(*atlas, glyphs);
    }

    TEST_F(FontAtlasScratchTest, EveryGlyphOfAnOutlineAtlasIsTheDilatedPictureOfTheFont) {
        const Vector<uint32_t> glyphs = PreloadGlyphs(*font);
        const IntrusivePtr<FontAtlas> atlas = New<FontAtlas>(font, kSmallSize, kPageSize, kOutlineRadius);

        EXPECT_TRUE(atlas->Prepare(*renderer, glyphs));

        ExpectPicturesMatchTheFont(*atlas, glyphs);
    }

    TEST_F(FontAtlasScratchTest, GlyphsAddedByASecondPrepareAreThePicturesOfTheFontToo) {
        const Vector<uint32_t> glyphs = PreloadGlyphs(*font);
        const size_t half = glyphs.size() / 2;
        const IntrusivePtr<FontAtlas> atlas = New<FontAtlas>(font, kBigSize, kPageSize);

        EXPECT_TRUE(atlas->Prepare(*renderer, Span<const uint32_t>(glyphs.data(), half)));
        const size_t pagesAfterFirst = atlas->GetPageCount();
        EXPECT_TRUE(atlas->Prepare(*renderer, glyphs));

        EXPECT_GT(atlas->GetPageCount(), pagesAfterFirst);
        ExpectPicturesMatchTheFont(*atlas, glyphs);
    }

    // What remains on the heap is the atlas's own: a node of the glyph table per glyph and the page buffers and textures. The glyph bitmaps, the
    // stb_truetype blocks and the lists of a build are in the scratch arena, which is given back when the call ends
    TEST_F(FontAtlasScratchTest, APlainPreloadMakesOnlyTheGlyphTableAndThePagesOnTheHeap) {
        const IntrusivePtr<FontAtlas> warmUp = New<FontAtlas>(font, kSmallSize, kPageSize);  // the first build of a thread takes its arena chunk
        warmUp->PrepareCodePoints(*renderer, TextSettings::Default().preload);
        const IntrusivePtr<FontAtlas> atlas = New<FontAtlas>(font, kBigSize, kPageSize);
        const size_t usedBefore = ScratchArena::Get().GetBytesUsed();

        const AllocCount counts = CountAllocations([&] { EXPECT_TRUE(atlas->PrepareCodePoints(*renderer, TextSettings::Default().preload)); });

        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), usedBefore);
        EXPECT_GT(atlas->GetPageCount(), 1u);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(counts.count, atlas->GetGlyphCount() + atlas->GetPageCount() * kBlocksPerPage + kBlocksSlack);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST_F(FontAtlasScratchTest, AnOutlinePreloadMakesOnlyTheGlyphTableAndThePagesOnTheHeap) {
        const IntrusivePtr<FontAtlas> warmUp = New<FontAtlas>(font, kSmallSize, kPageSize, kOutlineRadius);
        warmUp->PrepareCodePoints(*renderer, TextSettings::Default().preload);
        const IntrusivePtr<FontAtlas> atlas = New<FontAtlas>(font, kBigSize, kPageSize, kOutlineRadius);
        const size_t usedBefore = ScratchArena::Get().GetBytesUsed();

        const AllocCount counts = CountAllocations([&] { EXPECT_TRUE(atlas->PrepareCodePoints(*renderer, TextSettings::Default().preload)); });

        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), usedBefore);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(counts.count, atlas->GetGlyphCount() + atlas->GetPageCount() * kBlocksPerPage + kBlocksSlack);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    /// A benchmark, not a check: the counts are zero without the profile preset (`kNoAllocTracker`)
    TEST_F(FontAtlasScratchBenchTest, DISABLED_BuildOfAnAtlas) {
        FontAtlasLibrary& library = Library();
        (void)MeasureBuild(library, *renderer, font, kSmallSize, 0);  // the first build takes the thread's arena chunks and warms the caches
        Print("FontAtlasPlain24", MedianBuild(library, *renderer, font, kSmallSize, 0));
        Print("FontAtlasPlain48", MedianBuild(library, *renderer, font, kBigSize, 0));
        Print("FontAtlasOutline24r2", MedianBuild(library, *renderer, font, kSmallSize, kOutlineRadius));
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
