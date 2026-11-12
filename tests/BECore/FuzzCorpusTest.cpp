// The corpus of every fuzz target replayed, then mutated by the deterministic mutator: the part of fuzzing that runs in every build and on
// every platform. The long runs are libFuzzer (tests/Fuzz/CLAUDE.md).

#include <Common/Printers.h>
#include <Fuzz/FuzzMutator.h>
#include <Fuzz/FuzzSeeds.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Tests {

    using Fuzz::FuzzBytes;
    using Fuzz::FuzzRandom;
    using Fuzz::FuzzTarget;

    namespace {

        /// Mutations per target in an ordinary run: about two seconds in a Debug build (BAGI_FUZZ_ITERATIONS=<n> replaces it)
        constexpr uint64_t kDefaultIterations = 50000;

        /// The seed of the generator; mixed with the name of the target so the targets do not mutate the same way
        constexpr uint64_t kReplaySeed = 0x42414749465A5A31ull;

        constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001B3ull;

        /// Most mutations stacked on one seed
        constexpr uint32_t kMaxStackedMutations = 3;

        struct CorpusFile {
            std::string name;
            FuzzBytes bytes;
        };

        uint64_t IterationCount() {
            const char* text = std::getenv("BAGI_FUZZ_ITERATIONS");
            if (text == nullptr || *text == '\0') {
                return kDefaultIterations;
            }
            uint64_t value = 0;
            const char* end = text + std::strlen(text);
            const auto result = std::from_chars(text, end, value);
            return (result.ec == std::errc() && result.ptr == end) ? value : kDefaultIterations;
        }

        uint64_t HashName(std::string_view name) {
            uint64_t value = kFnvOffset;
            for (const char symbol : name) {
                value = (value ^ static_cast<uint8_t>(symbol)) * kFnvPrime;
            }
            return value;
        }

        /// The files of a corpus folder in the order of their names (the order of a directory listing is not defined)
        std::vector<CorpusFile> LoadCorpus(const std::filesystem::path& folder) {
            std::vector<CorpusFile> files;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                std::ifstream stream(entry.path(), std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                CorpusFile file;
                file.name = entry.path().filename().string();
                file.bytes.assign(reinterpret_cast<const uint8_t*>(content.data()), reinterpret_cast<const uint8_t*>(content.data()) + content.size());
                files.push_back(std::move(file));
            }
            std::sort(files.begin(), files.end(), [](const CorpusFile& a, const CorpusFile& b) { return a.name < b.name; });
            return files;
        }

        const FuzzTarget& FindTarget(const std::string& name) {
            const std::span<const FuzzTarget> targets = Fuzz::FuzzTargetList();
            const auto found = std::find_if(targets.begin(), targets.end(), [&](const FuzzTarget& target) { return name == target.name; });
            return *found;
        }

    }  // namespace

    /// The parameter of a case is the name of its target. It prints bare, because ctest names the case after what gtest prints for it
    struct FuzzCase {
        std::string name;
    };

    void PrintTo(const FuzzCase& fuzzCase, std::ostream* stream) {
        *stream << fuzzCase.name;
    }

    namespace {

        std::vector<FuzzCase> AllCases() {
            std::vector<FuzzCase> cases;
            for (const FuzzTarget& target : Fuzz::FuzzTargetList()) {
                cases.push_back({target.name});
            }
            return cases;
        }

    }  // namespace

    class FuzzReplayTest : public ::testing::TestWithParam<FuzzCase> {};

    TEST_P(FuzzReplayTest, ReplaysTheCorpusAndMutationsOfIt) {
        const FuzzTarget& target = FindTarget(GetParam().name);
        const std::filesystem::path folder = std::filesystem::path(BAGI_FUZZ_CORPUS_DIR) / target.corpusFolder;

        const std::vector<CorpusFile> files = LoadCorpus(folder);
        ASSERT_FALSE(files.empty()) << "no seeds in " << folder.string();

        std::vector<FuzzBytes> seeds;
        for (const CorpusFile& file : files) {
            EXPECT_LE(file.bytes.size(), Fuzz::kMaxFuzzInputSize) << file.name << " is larger than the inputs the fuzzers make";
            seeds.push_back(file.bytes);
            target.function(file.bytes.data(), file.bytes.size());
        }

        FuzzRandom random(kReplaySeed ^ HashName(target.name));
        const uint64_t iterations = IterationCount();
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
            const std::vector<CorpusFile> files = LoadCorpus(std::filesystem::path(BAGI_FUZZ_CORPUS_DIR) / seed.folder);
            const auto found = std::find_if(files.begin(), files.end(), [&](const CorpusFile& file) { return file.name == seed.name; });
            ASSERT_NE(found, files.end()) << seed.name << " is not in the corpus: run WriteSeeds";
            EXPECT_TRUE(std::equal(found->bytes.begin(), found->bytes.end(), seed.bytes.begin(), seed.bytes.end())) << seed.name << " differs from what the writers make now: run WriteSeeds";
        }
    }

}  // namespace BECore::Tests
