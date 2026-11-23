// The heap cost of the listings and the editor save: what a call allocates for its working lists now that they live on the scratch arena.

#include <BECore/FileSystem/FileSystem.h>
#include <Common/AllocCheck.h>
#include <Common/FreshText.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kFolders = 10;
        constexpr size_t kFilesPerFolder = 30;
        /// What the OS layer of one listed file costs and stays on the heap (the iterator's path, the file's and its extension's text); the lists and the
        /// text of the result are not in it. Listing 300 files made 2748 allocations (9 per file) when the lists were on the heap, 937 after
        constexpr uint64_t kHeapBlocksPerFile = 4;

    }  // namespace

    TEST(ListingScratchTest, EnumerateFilesOfAFewHundredFilesAllocatesLittleBesideTheOsListing) {
        if (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        TestTempFolder folder("BagiListingScratch");
        const std::filesystem::path root = ToStdString(folder.PathOf("root"));
        const String stem = FreshText("listing");
        for (size_t d = 0; d < kFolders; ++d) {
            for (size_t f = 0; f < kFilesPerFolder; ++f) {
                WriteFile(root / "assets" / ToStd(Format("{}_{:02}", stem, d)) / ToStd(Format("f{:03}.png", f)), "x");
            }
        }

        FileSystem fs;
        fs.AddContentRoot(root);
        fs.Initialize();
        const PoolString assets = "assets"_intern;
        const StringView extensions[] = {".png"};

        // The first call interns the names; the second one measures the listing alone
        ASSERT_EQ(fs.EnumerateFiles(assets, Span<const StringView>(extensions)).size(), kFolders * kFilesPerFolder);
        Vector<PoolString> files;
        const AllocCount counts = CountAllocations([&] { files = fs.EnumerateFiles(assets, Span<const StringView>(extensions)); });

        ASSERT_EQ(files.size(), kFolders * kFilesPerFolder);
        EXPECT_LT(counts.count, kHeapBlocksPerFile * files.size());
    }

}  // namespace BECore::Tests
