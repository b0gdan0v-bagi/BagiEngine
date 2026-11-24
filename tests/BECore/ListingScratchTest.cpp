// The heap cost of the listings and the editor save: what a call allocates for its working lists now that they live on the scratch arena.

#include <BECore/Config/XmlDocument.h>
#include <BECore/Config/XmlLayoutMerge.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Memory/ScratchArena.h>
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

        constexpr int kConfigLines = 300;
        /// The merged text is the only heap block of a merge of a config that fits the stack chunk of its working memory; the line lists, the map, the
        /// diff and the hunks made 88 more before they moved there
        constexpr uint64_t kMergeHeapBlocks = 1;
        /// What a save of a 300-line config over its file still allocates: the file layer's paths, the two pugixml documents, the canonical
        /// texts and the result. The old text, its line lists and the diff made 5 more (44 before, 39 after)
        constexpr uint64_t kSaveHeapBlocks = 42;

        /// A config of @p lines rows with a hand-laid blank line after every tenth one, and its canonical text (the blank lines dropped)
        struct LaidOutConfig {
            String original;
            String canonical;
        };

        LaidOutConfig MakeConfig(int lines) {
            LaidOutConfig config;
            config.original = "<root>\n";
            config.canonical = "<root>\n";
            for (int i = 0; i < lines; ++i) {
                const String row = Format("  <row id=\"{}\" x=\"{}\" />\n", i, i * 7);
                config.original += row;
                config.canonical += row;
                if (i % 10 == 9) {
                    config.original += "\n";
                }
            }
            config.original += "</root>\n";
            config.canonical += "</root>\n";
            return config;
        }

        /// @p text with the row @p index replaced by another one
        String WithEditedRow(const String& text, int index) {
            String result = text;
            const String from = Format("<row id=\"{}\"", index);
            const size_t at = result.find(from.c_str());
            EXPECT_NE(at, String::npos);
            if (at != String::npos) {
                result.replace(at, from.size(), "<row edited=\"1\"");
            }
            return result;
        }

    }  // namespace

    TEST(ListingScratchTest, EnumerateFilesOfAFewHundredFilesAllocatesLittleBesideTheOsListing) {
        if (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        TestTempFolder folder("BagiListingScratch");
        const Path root = folder.PathOf("root");
        const String stem = FreshText("listing");
        for (size_t d = 0; d < kFolders; ++d) {
            for (size_t f = 0; f < kFilesPerFolder; ++f) {
                WriteFile(root / "assets" / Format("{}_{:02}", stem, d) / Format("f{:03}.png", f), "x");
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

    TEST(ListingScratchTest, MergingAFewEditsIntoAHandLaidConfigKeepsItsLayoutAndAllocatesLittle) {
        const LaidOutConfig config = MakeConfig(kConfigLines);
        String updated = WithEditedRow(config.canonical, 5);
        updated = WithEditedRow(updated, 150);
        updated = WithEditedRow(updated, 290);

        Optional<String> merged;
        const AllocCount counts = CountAllocations([&] { merged = MergeXmlLayout(config.original, config.canonical, updated); });

        ASSERT_TRUE(merged.has_value());
        // Every edited row replaces its line in place and the hand-laid blank lines stay
        String expected = WithEditedRow(config.original, 5);
        expected = WithEditedRow(expected, 150);
        expected = WithEditedRow(expected, 290);
        EXPECT_EQ(*merged, expected);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(counts.count, kMergeHeapBlocks);
        }
    }

    TEST(ListingScratchTest, AMergeThatNeedsTooManyEditsIsRefusedAndLeavesTheScratchArenaWhereItWas) {
        constexpr int kLines = 2500;
        String baseline = "<root>\n";
        String updated = "<root>\n";
        for (int i = 0; i < kLines; ++i) {
            baseline += Format("  <a n=\"{}\" />\n", i);
            updated += Format("  <b m=\"{}\" />\n", i);
        }
        baseline += "</root>\n";
        updated += "</root>\n";

        const size_t usedBefore = ScratchArena::Get().GetBytesUsed();
        const size_t capacityBefore = ScratchArena::Get().GetCapacity();
        EXPECT_FALSE(MergeXmlLayout(baseline, baseline, updated).has_value());
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), usedBefore);
        // A scratch arena keeps its chunks after a scope: the diff of a pathological edit count must not have grown it
        EXPECT_EQ(ScratchArena::Get().GetCapacity(), capacityBefore);
    }

    TEST(ListingScratchTest, SavingAnEditedConfigOverItsFileKeepsItsLayoutAndReadsTheOldTextFromScratch) {
        TestTempFolder folder("BagiListingScratchSave");
        const Path path = folder.PathOf("config.xml");
        const LaidOutConfig config = MakeConfig(kConfigLines);
        WriteFile(path, config.original);

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(path));
        pugi::xml_node row = document.GetRoot().GetPugiNode().first_child();
        row.attribute("x").set_value("edited");

        bool saved = false;
        const AllocCount counts = CountAllocations([&] { saved = document.SaveToFile(path); });

        ASSERT_TRUE(saved);
        String expected = config.original;
        const size_t at = expected.find("x=\"0\"");
        ASSERT_NE(at, String::npos);
        expected.replace(at, 5, "x=\"edited\"");
        EXPECT_EQ(ReadFile(path), expected);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(counts.count, kSaveHeapBlocks);
        }
    }

}  // namespace BECore::Tests
