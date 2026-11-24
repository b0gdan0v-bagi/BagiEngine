// The corpus of every fuzz target replayed, then mutated by the deterministic mutator: the part of fuzzing that runs in every build and on
// every platform. The everyday run only replays; the mutations need BAGI_FUZZ_ITERATIONS, which the ctest entry BagiFuzzMutations (label
// extended) sets. The long runs are libFuzzer (tests/Fuzz/CLAUDE.md).

#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzMutator.h>
#include <Fuzz/FuzzSeeds.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Tests {

    using Fuzz::FuzzBytes;
    using Fuzz::FuzzRandom;
    using Fuzz::FuzzTarget;

    namespace {

        /// The seed of the generator; mixed with the name of the target so the targets do not mutate the same way
        constexpr uint64_t kReplaySeed = 0x42414749465A5A31ull;

        /// Most mutations stacked on one seed
        constexpr uint32_t kMaxStackedMutations = 3;

        /// The committed seeds, tests/Fuzz/corpus of the engine
        Path CorpusRoot() {
            return Path(BagiContentRoots::kEngine) / "tests" / "Fuzz" / "corpus";
        }

        struct CorpusFile {
            String name;
            FuzzBytes bytes;
        };

        /// BAGI_FUZZ_ITERATIONS divided by the cost weight, so a target whose input costs more takes about the same time; none without it
        /// (or when it is not a number): the mutations belong to the extended run, the replay of the corpus to every run
        uint64_t IterationCount(const FuzzTarget& target) {
            const char* text = std::getenv("BAGI_FUZZ_ITERATIONS");
            if (text == nullptr || *text == '\0') {
                return 0;
            }
            uint64_t value = 0;
            const char* end = text + std::strlen(text);
            const auto result = std::from_chars(text, end, value);
            if (result.ec != std::errc() || result.ptr != end) {
                return 0;
            }
            return value / std::max<uint32_t>(target.costWeight, 1);
        }

        uint64_t HashName(StringView name) {
            Fuzz::FuzzDigest digest;
            digest.AddBytes(name.data(), name.size());
            return digest.Value();
        }

        /// The files of a corpus folder in the order of their names (the order of a directory listing is not defined)
        Vector<CorpusFile> LoadCorpus(const Path& folder) {
            Vector<CorpusFile> files;
            const auto walked = WalkDirectory(folder, WalkMode::Folder, [&](StringView name, FileKind kind) {
                if (kind == FileKind::File) {
                    CorpusFile file;
                    file.name = String(name);
                    file.bytes = ReadFileBytes(folder / name);
                    files.push_back(std::move(file));
                }
                return true;
            });
            EXPECT_TRUE(walked.has_value()) << "could not list " << folder.CStr() << ": " << (walked ? "" : walked.error().c_str());
            std::sort(files.begin(), files.end(), [](const CorpusFile& a, const CorpusFile& b) { return a.name < b.name; });
            return files;
        }

        const FuzzTarget& FindTarget(const String& name) {
            const Span<const FuzzTarget> targets = Fuzz::FuzzTargetList();
            const auto found = std::find_if(targets.begin(), targets.end(), [&](const FuzzTarget& target) { return name == target.name; });
            return *found;
        }

    }  // namespace

    /// The parameter of a case is the name of its target. It prints bare, because ctest names the case after what gtest prints for it
    struct FuzzCase {
        String name;
    };

    void PrintTo(const FuzzCase& fuzzCase, std::ostream* stream) {  //-NoStd: gtest's PrintTo signature
        *stream << fuzzCase.name;
    }

    namespace {

        Vector<FuzzCase> AllCases() {
            Vector<FuzzCase> cases;
            for (const FuzzTarget& target : Fuzz::FuzzTargetList()) {
                cases.push_back({target.name});
            }
            return cases;
        }

    }  // namespace

    class FuzzReplayTest : public ::testing::TestWithParam<FuzzCase> {};

    TEST_P(FuzzReplayTest, ReplaysTheCorpusAndMutationsOfIt) {
        const FuzzTarget& target = FindTarget(GetParam().name);
        const Path folder = CorpusRoot() / target.corpusFolder;

        const Vector<CorpusFile> files = LoadCorpus(folder);
        ASSERT_FALSE(files.empty()) << "no seeds in " << folder.CStr();

        Vector<FuzzBytes> seeds;
        for (const CorpusFile& file : files) {
            EXPECT_LE(file.bytes.size(), Fuzz::kMaxFuzzInputSize) << file.name << " is larger than the inputs the fuzzers make";
            seeds.push_back(file.bytes);
            target.function(file.bytes.data(), file.bytes.size());
        }

        FuzzRandom random(kReplaySeed ^ HashName(target.name));
        const uint64_t iterations = IterationCount(target);
        for (uint64_t i = 0; i < iterations; ++i) {
            FuzzBytes input = seeds[i % seeds.size()];
            const uint32_t stacked = 1 + random.Below(kMaxStackedMutations);
            for (uint32_t step = 0; step < stacked; ++step) {
                Fuzz::Mutate(input, random, seeds);
            }
            target.function(input.data(), input.size());
        }
    }

    INSTANTIATE_TEST_SUITE_P(FuzzCorpus, FuzzReplayTest, ::testing::ValuesIn(AllCases()));

    // The generated seeds are committed; this writes them again after a format changed:
    //   BAGI_FUZZ_WRITE_SEEDS=<repo>/tests/Fuzz/corpus build/<preset>/bin/BagiEngineTests --gtest_filter=FuzzCorpusTest.WriteSeeds
    TEST(FuzzCorpusTest, WriteSeeds) {
        const char* folder = std::getenv(Fuzz::kWriteSeedsVariable);
        if (folder == nullptr || *folder == '\0') {
            GTEST_SKIP() << "set " << Fuzz::kWriteSeedsVariable << " to the corpus folder to write the generated seeds";
        }
        EXPECT_TRUE(Fuzz::WriteGeneratedSeeds(folder));
    }

    TEST(FuzzCorpusTest, GeneratedSeedsAreTheCommittedFiles) {
        for (const Fuzz::FuzzSeedFile& seed : Fuzz::MakeGeneratedSeeds()) {
            const Vector<CorpusFile> files = LoadCorpus(CorpusRoot() / Path(seed.folder));  //-NoStd: seed.folder is a std::filesystem::path until FuzzSeeds.h moves (stage 6)
            const auto found = std::find_if(files.begin(), files.end(), [&](const CorpusFile& file) { return file.name == seed.name; });
            ASSERT_NE(found, files.end()) << seed.name << " is not in the corpus: run WriteSeeds";
            EXPECT_TRUE(std::equal(found->bytes.begin(), found->bytes.end(), seed.bytes.begin(), seed.bytes.end())) << seed.name << " differs from what the writers make now: run WriteSeeds";
        }
    }

}  // namespace BECore::Tests
