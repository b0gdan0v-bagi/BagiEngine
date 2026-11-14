#include <Common/TempPath.h>
#include <Match3/Game/Match3GameModel.h>
#include <Match3/Game/Match3SaveState.h>
#include <Match3/Game/MatchFinder.h>

/**
 * A damaged or hand-edited save of a real game through its consumer, LoadMatch3:
 *   - a sweep of mutated bytes of a real save: a refusal leaves the model exactly as it was and says why in one short
 *     sentence, an acceptance leaves a model that is a legal game;
 *   - edits of the state (not of the bytes) that must each be refused with their own kind;
 *   - what is in the place of the slot file: a folder, an empty file, a file over the size limit.
 *
 * The mutator is local: Match3 builds in its own repository too, where the engine's fuzz library (tests/Fuzz) does not exist.
 */
namespace BECore::Match3::Tests {

    namespace {

        constexpr StringView kSlot = "damaged";

        /// Mutations of the sweep, and the most one input gets stacked
        constexpr int kSweepCount = 3000;
        constexpr uint32_t kMostStackedMutations = 3;

        /// Longest sentence a refusal may be: it reaches the log and the screen of the player
        constexpr size_t kMaxDetailBytes = 300;

        /// xorshift64*: a fixed seed gives the same bytes on every platform (the standard distributions do not)
        class SweepRandom {
        public:
            explicit SweepRandom(uint64_t seed) : _state(seed) {}

            uint64_t Next() {
                _state ^= _state >> 12;
                _state ^= _state << 25;
                _state ^= _state >> 27;
                return _state * 0x2545F4914F6CDD1DULL;
            }

            /// A number in [0, bound); 0 when @p bound is 0
            size_t Below(size_t bound) {
                return bound == 0 ? 0 : static_cast<size_t>(Next() >> 11) % bound;
            }

        private:
            uint64_t _state;
        };

        /// One change of the bytes: a flipped bit, a byte or a 4-byte number at an edge value, a block inserted, deleted or doubled, a cut
        void Mutate(eastl::vector<uint8_t>& bytes, SweepRandom& random) {
            constexpr uint8_t kEdgeBytes[] = {0x00, 0x7F, 0x80, 0xFF};
            constexpr size_t kWordSize = 4;
            constexpr size_t kMostBlockBytes = 16;

            if (bytes.empty()) {
                bytes.push_back(static_cast<uint8_t>(random.Next()));
                return;
            }
            const size_t at = random.Below(bytes.size());
            const size_t block = 1 + random.Below(std::min(kMostBlockBytes, bytes.size() - at));
            switch (random.Below(7)) {
                case 0:
                    bytes[at] ^= static_cast<uint8_t>(1u << random.Below(8));
                    break;
                case 1:
                    bytes[at] = kEdgeBytes[random.Below(std::size(kEdgeBytes))];
                    break;
                case 2: {
                    const uint8_t fill = kEdgeBytes[random.Below(std::size(kEdgeBytes))];
                    for (size_t i = at; i < std::min(at + kWordSize, bytes.size()); ++i) {
                        bytes[i] = fill;
                    }
                    break;
                }
                case 3:
                    bytes.insert(bytes.begin() + static_cast<ptrdiff_t>(at), block, static_cast<uint8_t>(random.Next()));
                    break;
                case 4:
                    bytes.erase(bytes.begin() + static_cast<ptrdiff_t>(at), bytes.begin() + static_cast<ptrdiff_t>(at + block));
                    break;
                case 5:
                    bytes.insert(bytes.begin() + static_cast<ptrdiff_t>(at), bytes.begin() + static_cast<ptrdiff_t>(at), bytes.begin() + static_cast<ptrdiff_t>(at + block));
                    break;
                default:
                    bytes.resize(at);
                    break;
            }
        }

        /// Reset to the seed and play two swaps, so that the score and the generator have moved
        void PlaySwaps(Match3GameModel& model, uint32_t seed, int count) {
            model.Reset(seed);
            for (int i = 0; i < count; ++i) {
                const auto swaps = model.GetLegalSwaps();
                ASSERT_FALSE(swaps.empty());
                ASSERT_TRUE(model.TrySwap(swaps[0].first, swaps[0].second));
            }
        }

        /// Everything of a model that a refused load must leave alone, selection and last swap included
        struct Snapshot {
            uint64_t version = 0;
            Match3SaveState state;
            bool hasSelection = false;
            bool hasLastSwap = false;

            explicit Snapshot(const Match3GameModel& model)
                : version(model.GetVersion()), state(model.CaptureState()), hasSelection(model.HasSelection()), hasLastSwap(model.GetLastSwap() != nullptr) {}

            bool operator==(const Snapshot& other) const {
                return version == other.version && state.seed == other.state.seed && state.score == other.state.score && state.board == other.state.board && state.rng == other.state.rng &&
                       hasSelection == other.hasSelection && hasLastSwap == other.hasLastSwap;
            }
        };

        bool IsSettledBoardOfGems(const Match3GameModel& model) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    if (model.GetCellAt(Cell{col, row}) == GemType::None) {
                        return false;
                    }
                }
            }
            return !MatchFinder::HasAnyMatch(model.GetBoard());
        }

        /// A refusal is one sentence for the log and the player
        void ExpectShortSentence(const SaveError& error, const char* what) {
            EXPECT_FALSE(error.detail.empty()) << what;
            EXPECT_LE(error.detail.size(), kMaxDetailBytes) << what;
        }

    }  // namespace

    /// A local FileSystem and a temp folder per test, like Match3SaveFileTest: no global services
    class Match3SaveDamageTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _base = BECore::Tests::TestTempDirectory("BagiMatch3SaveDamage");
            std::filesystem::remove_all(_base);
            std::filesystem::create_directories(_base);
            _service = std::make_unique<SaveGameService>(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_base / "saves"));

            // The game on the screen when the damaged file is loaded: played, with a selection and a last swap, so that a changed field shows
            PlaySwaps(_model, 31, 2);
            _model.OnCellClicked(Cell{1, 1});
        }

        void TearDown() override {
            _service.reset();
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        std::filesystem::path SlotFile() const {
            return _service->GetDirectory() / "damaged.save";
        }

        /// The state edited by @p edit, saved under @p schema as the slot; true when the service wrote it
        template <typename Edit>
        bool SaveEdited(const SaveSchema& schema, Edit&& edit) {
            Match3GameModel source;
            PlaySwaps(source, 77, 2);
            Match3SaveState state = source.CaptureState();
            edit(state);
            return _service->Save(kSlot, schema, state).has_value();
        }

        /// Load the slot into the model of the fixture: refused with @p kind and the model untouched
        void ExpectRefused(SaveErrorKind kind, const char* what) {
            const Snapshot before(_model);
            const auto info = LoadMatch3(*_service, kSlot, _model);
            ASSERT_FALSE(info.has_value()) << what;
            EXPECT_EQ(info.error().kind, kind) << what;
            ExpectShortSentence(info.error(), what);
            EXPECT_TRUE(Snapshot(_model) == before) << what;
        }

        std::filesystem::path _base;
        FileSystem _fileSystem;
        std::unique_ptr<SaveGameService> _service;
        Match3GameModel _model;
    };

    TEST_F(Match3SaveDamageTest, MutatedBytesOfARealSaveAreRefusedWithTheModelUntouchedOrLoadAGoodGame) {
        Match3GameModel source;
        PlaySwaps(source, 2024, 3);
        ASSERT_TRUE(SaveMatch3(*_service, kSlot, source).has_value());
        const auto original = _service->ReadSlot(kSlot);
        ASSERT_TRUE(original.has_value());

        SweepRandom random(0x5EED5A7E);
        int refused = 0;
        int accepted = 0;
        for (int round = 0; round < kSweepCount; ++round) {
            eastl::vector<uint8_t> bytes = *original;
            const size_t stacked = 1 + random.Below(kMostStackedMutations);
            for (size_t i = 0; i < stacked; ++i) {
                Mutate(bytes, random);
            }
            ASSERT_TRUE(_service->WriteSlot(kSlot, bytes).has_value()) << "round " << round;

            const Snapshot before(_model);
            const auto info = LoadMatch3(*_service, kSlot, _model);
            if (!info) {
                ++refused;
                ExpectShortSentence(info.error(), "a mutated save");
                ASSERT_TRUE(Snapshot(_model) == before) << "round " << round << ": a refused load changed the model (" << info.error().detail.c_str() << ")";
                continue;
            }

            // Accepted: the file was still a save, perhaps with another seed or score, and what is loaded is a game
            ++accepted;
            ASSERT_TRUE(IsSettledBoardOfGems(_model)) << "round " << round;
            ASSERT_GE(_model.GetScore(), 0) << "round " << round;
            EXPECT_EQ(_model.GetVersion(), before.version + 1) << "round " << round;
            const auto swaps = _model.GetLegalSwaps();
            if (!swaps.empty()) {
                const int score = _model.GetScore();
                ASSERT_TRUE(_model.TrySwap(swaps[0].first, swaps[0].second)) << "round " << round;
                EXPECT_GE(_model.GetScore(), score) << "round " << round;
                EXPECT_TRUE(IsSettledBoardOfGems(_model)) << "round " << round;
            }
            // The next round starts from a model that was played on
            _model.OnCellClicked(Cell{1, 1});
        }

        // The seed is fixed, so the sweep is the same every time: it has to reach both outcomes, or it tests less than it says
        EXPECT_GT(refused, 0);
        EXPECT_GT(accepted, 0);
    }

    TEST_F(Match3SaveDamageTest, EditedBoardIsCorruptAndTheModelIsKept) {
        struct Case {
            const char* name;
            void (*edit)(Match3SaveState&);
        };
        const Case cases[] = {
            {"board of 63 letters", [](Match3SaveState& s) { s.board.pop_back(); }},
            {"board of 65 letters", [](Match3SaveState& s) { s.board.push_back('R'); }},
            {"letter that is no gem", [](Match3SaveState& s) { s.board[20] = 'Z'; }},
            {"board with a match", [](Match3SaveState& s) { s.board[8] = s.board[9] = s.board[10] = 'G'; }},
            {"board of a megabyte", [](Match3SaveState& s) { s.board.assign(1024 * 1024, 'R'); }},
        };
        for (const Case& c : cases) {
            ASSERT_TRUE(SaveEdited(kMatch3SaveSchema, c.edit)) << c.name;
            ExpectRefused(SaveErrorKind::Corrupt, c.name);
        }
    }

    TEST_F(Match3SaveDamageTest, EditedScoreIsCorruptAndTheModelIsKept) {
        ASSERT_TRUE(SaveEdited(kMatch3SaveSchema, [](Match3SaveState& s) { s.score = -1; }));
        ExpectRefused(SaveErrorKind::Corrupt, "negative score");

        ASSERT_TRUE(SaveEdited(kMatch3SaveSchema, [](Match3SaveState& s) { s.score = std::numeric_limits<int32_t>::min(); }));
        ExpectRefused(SaveErrorKind::Corrupt, "INT32_MIN");
    }

    TEST_F(Match3SaveDamageTest, EditedGeneratorTextIsCorruptAndTheModelIsKept) {
        struct Case {
            const char* name;
            void (*edit)(Match3SaveState&);
        };
        const Case cases[] = {
            {"generator text of a megabyte", [](Match3SaveState& s) { s.rng.assign(1024 * 1024, '1'); }},
            {"generator numbers of a megabyte",
             [](Match3SaveState& s) {
                 s.rng.clear();
                 for (size_t i = 0; i < 512 * 1024; ++i) {
                     s.rng.append("1 ");
                 }
             }},
            {"generator of digits only", [](Match3SaveState& s) { s.rng.assign(5000, '7'); }},
            {"generator text with a trailing space", [](Match3SaveState& s) { s.rng.push_back(' '); }},
            {"generator text with a leading space", [](Match3SaveState& s) { s.rng.insert(0, " "); }},
            {"generator number with a leading zero", [](Match3SaveState& s) { s.rng.insert(0, "0"); }},
            {"generator text with a minus", [](Match3SaveState& s) { s.rng.insert(0, "-"); }},
        };
        for (const Case& c : cases) {
            ASSERT_TRUE(SaveEdited(kMatch3SaveSchema, c.edit)) << c.name;
            ExpectRefused(SaveErrorKind::Corrupt, c.name);
        }
    }

    TEST_F(Match3SaveDamageTest, SchemaOfAnotherVersionOrGameIsRefusedWithItsOwnKind) {
        const auto keep = [](Match3SaveState&) {};

        ASSERT_TRUE(SaveEdited(SaveSchema{kMatch3SaveGame, kMatch3SaveVersion + 1, kMatch3SaveVersion}, keep));
        ExpectRefused(SaveErrorKind::VersionTooNew, "one version up");

        ASSERT_GT(kMatch3SaveMinVersion, 0u);
        ASSERT_TRUE(SaveEdited(SaveSchema{kMatch3SaveGame, kMatch3SaveMinVersion - 1, 0}, keep));
        ExpectRefused(SaveErrorKind::VersionTooOld, "one version down");

        ASSERT_TRUE(SaveEdited(SaveSchema{"Chess", kMatch3SaveVersion, kMatch3SaveMinVersion}, keep));
        ExpectRefused(SaveErrorKind::WrongGame, "another game's id");
    }

    TEST_F(Match3SaveDamageTest, WhatIsInThePlaceOfTheSlotFileIsRefusedWithTheModelKept) {
        // A folder where the file should be
        std::filesystem::create_directories(SlotFile());
        ExpectRefused(SaveErrorKind::ReadFailed, "a folder in place of the slot file");
        std::filesystem::remove_all(SlotFile());

        // A file of no bytes is not a save
        {
            std::ofstream empty(SlotFile(), std::ios::binary);
            ASSERT_TRUE(empty.good());
        }
        ASSERT_EQ(std::filesystem::file_size(SlotFile()), 0u);
        const Snapshot before(_model);
        const auto info = LoadMatch3(*_service, kSlot, _model);
        ASSERT_FALSE(info.has_value());
        EXPECT_NE(info.error().kind, SaveErrorKind::NotFound);
        ExpectShortSentence(info.error(), "an empty file");
        EXPECT_TRUE(Snapshot(_model) == before);

        // One byte over the limit of the reader: refused before it is read
        std::filesystem::resize_file(SlotFile(), ReaderLimits::kMaxBinaryStreamBytes + 1);
        ExpectRefused(SaveErrorKind::ReadFailed, "a file one byte over the size limit");
    }

}  // namespace BECore::Match3::Tests
