#include "SaveGameTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Save/SaveGame.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Generated/SaveGameTest.gen.hpp>

namespace BECore::Tests {

    using namespace SaveTestData;

    namespace {

        constexpr eastl::string_view kGame = "TestGame";

        /// The game as it is now (version 2, reads from 1)
        constexpr SaveSchema kSchema{kGame, 2, 1};

        Item MakeItem(int32_t id, float weight, eastl::string_view label) {
            Item item;
            item.id = id;
            item.weight = weight;
            item.label = label;
            return item;
        }

        GameState MakeState() {
            GameState state;
            state.score = 1234;
            state.seed = 4000000000u;
            state.ticks = -9000000000ll;
            state.speed = 2.25f;
            state.hardcore = true;
            state.title = "Level three";
            state.hero = PoolString::Intern("Hero");
            state.difficulty = Difficulty::Hard;
            state.position.x = -7;
            state.position.y = 42;
            state.items.push_back(MakeItem(10, 0.5f, "sword"));
            state.items.push_back(MakeItem(11, 12.0f, ""));
            state.items.push_back(MakeItem(12, -1.0f, "shield"));
            return state;
        }

        void ExpectSameState(const GameState& expected, const GameState& actual) {
            EXPECT_EQ(actual.score, expected.score);
            EXPECT_EQ(actual.seed, expected.seed);
            EXPECT_EQ(actual.ticks, expected.ticks);
            EXPECT_EQ(actual.speed, expected.speed);
            EXPECT_EQ(actual.hardcore, expected.hardcore);
            EXPECT_EQ(actual.title, expected.title);
            EXPECT_EQ(actual.hero, expected.hero);
            EXPECT_EQ(actual.difficulty, expected.difficulty);
            EXPECT_EQ(actual.position.x, expected.position.x);
            EXPECT_EQ(actual.position.y, expected.position.y);
            ASSERT_EQ(actual.items.size(), expected.items.size());
            for (size_t i = 0; i < expected.items.size(); ++i) {
                SCOPED_TRACE(i);
                EXPECT_EQ(actual.items[i].id, expected.items[i].id);
                EXPECT_EQ(actual.items[i].weight, expected.items[i].weight);
                EXPECT_EQ(actual.items[i].label, expected.items[i].label);
            }
        }

        /// A state that nothing in a test file writes (score -5, nothing else): proof that a refused load did not touch it
        GameState MakeUntouchedState() {
            GameState state;
            state.score = -5;
            return state;
        }

        void ExpectUntouched(const GameState& state) {
            EXPECT_EQ(state.score, -5);
            EXPECT_TRUE(state.title.empty());
            EXPECT_TRUE(state.items.empty());
        }

        template <typename T>
        void ExpectFailure(const std::expected<T, SaveError>& result, SaveErrorKind kind) {
            ASSERT_FALSE(result.has_value()) << "a failure was expected";
            EXPECT_EQ(result.error().kind, kind);
            EXPECT_FALSE(result.error().detail.empty());
        }

        /// The bytes of a valid save file of GameState
        eastl::vector<uint8_t> ValidBytes() {
            BinarySerializer serializer;
            serializer.BeginObject("save");
            serializer.WriteAttribute("game", kGame);
            serializer.WriteAttribute("version", uint32_t{2});
            serializer.BeginObject("state");
            MakeState().Serialize(serializer);
            serializer.EndObject();
            serializer.EndObject();
            return serializer.GetData();
        }

        /// A stream with the root object `save` that has the given game and version and, when asked, an empty `state`
        eastl::vector<uint8_t> EnvelopeBytes(bool withGame, bool withVersion, bool versionIsText, bool withState) {
            BinarySerializer serializer;
            serializer.BeginObject("save");
            if (withGame) {
                serializer.WriteAttribute("game", kGame);
            }
            if (withVersion && versionIsText) {
                serializer.WriteAttribute("version", eastl::string_view("two"));
            } else if (withVersion) {
                serializer.WriteAttribute("version", uint32_t{2});
            }
            if (withState) {
                serializer.BeginObject("state");
                serializer.EndObject();
            }
            serializer.EndObject();
            return serializer.GetData();
        }

    }  // namespace

    /// A local FileSystem and a temp folder per test: no global services, so the tests run beside each other
    class SaveGameTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _base = TestTempDirectory("BagiSaveGameEnvelopeTest");
            std::filesystem::remove_all(_base);
            std::filesystem::create_directories(_base);
            _saves = _base / "saves";
            _service.emplace(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_saves));
        }

        void TearDown() override {
            _service.reset();
            std::error_code ec;
            std::filesystem::remove_all(_base, ec);
        }

        SaveGameService& Service() {
            return *_service;
        }

        std::filesystem::path _base;
        std::filesystem::path _saves;
        FileSystem _fileSystem;
        std::optional<SaveGameService> _service;
    };

    TEST_F(SaveGameTest, StateRoundTripsFieldByField) {
        const GameState original = MakeState();
        ASSERT_TRUE(Service().Save("slot", kSchema, original).has_value());
        EXPECT_TRUE(Service().Exists("slot"));

        GameState loaded = MakeUntouchedState();
        loaded.items.push_back(MakeItem(99, 9.0f, "stale"));  // a load replaces the vector, it does not append
        const auto info = Service().Load("slot", kSchema, loaded);
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(info->game, "TestGame");
        EXPECT_EQ(info->version, 2u);
        ExpectSameState(original, loaded);
    }

    TEST_F(SaveGameTest, SavedStateOverwritesTheSlot) {
        const GameState first = MakeState();
        GameState second = MakeState();
        second.score = 1;
        second.items.clear();

        ASSERT_TRUE(Service().Save("slot", kSchema, first).has_value());
        ASSERT_TRUE(Service().Save("slot", kSchema, second).has_value());

        GameState loaded;
        ASSERT_TRUE(Service().Load("slot", kSchema, loaded).has_value());
        ExpectSameState(second, loaded);
    }

    TEST_F(SaveGameTest, PeekGivesGameAndVersionWithoutTheState) {
        ASSERT_TRUE(Service().Save("slot", SaveSchema{"Other", 7, 5}, MakeState()).has_value());

        const auto info = Service().Peek("slot");
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->game, "Other");
        EXPECT_EQ(info->version, 7u);
    }

    TEST_F(SaveGameTest, PeekReadsAFileWhoseStateIsMissing) {
        ASSERT_TRUE(Service().WriteSlot("slot", EnvelopeBytes(true, true, false, false)).has_value());

        const auto info = Service().Peek("slot");
        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->game, "TestGame");
        EXPECT_EQ(info->version, 2u);
    }

    TEST_F(SaveGameTest, PeekFailuresAreClean) {
        ExpectFailure(Service().Peek("nothing"), SaveErrorKind::NotFound);
        ExpectFailure(Service().Peek("../x"), SaveErrorKind::InvalidSlotName);

        ASSERT_TRUE(Service().WriteSlot("junk", eastl::vector<uint8_t>(64, 0xAB)).has_value());
        ExpectFailure(Service().Peek("junk"), SaveErrorKind::Corrupt);
    }

    TEST_F(SaveGameTest, SaveOfAnotherGameIsWrongGame) {
        ASSERT_TRUE(Service().Save("slot", SaveSchema{"Chess", 2, 1}, MakeState()).has_value());

        GameState state = MakeUntouchedState();
        ExpectFailure(Service().Load("slot", kSchema, state), SaveErrorKind::WrongGame);
        ExpectUntouched(state);
    }

    TEST_F(SaveGameTest, NewerVersionIsRefusedAndTheStateIsUntouched) {
        ASSERT_TRUE(Service().Save("slot", SaveSchema{kGame, 3, 1}, MakeState()).has_value());

        GameState state = MakeUntouchedState();
        ExpectFailure(Service().Load("slot", kSchema, state), SaveErrorKind::VersionTooNew);
        ExpectUntouched(state);
    }

    TEST_F(SaveGameTest, VersionBelowMinVersionIsRefused) {
        ASSERT_TRUE(Service().Save("slot", SaveSchema{kGame, 1, 1}, MakeState()).has_value());

        GameState state = MakeUntouchedState();
        ExpectFailure(Service().Load("slot", SaveSchema{kGame, 3, 2}, state), SaveErrorKind::VersionTooOld);
        ExpectUntouched(state);
    }

    TEST_F(SaveGameTest, RefusedLoadDoesNotCallTheReader) {
        ASSERT_TRUE(Service().Save("slot", SaveSchema{kGame, 3, 1}, MakeState()).has_value());

        int calls = 0;
        const auto result = Service().Load("slot", kSchema, [&calls](IDeserializer&) { ++calls; });
        ExpectFailure(result, SaveErrorKind::VersionTooNew);
        EXPECT_EQ(calls, 0);
    }

    TEST_F(SaveGameTest, OlderAcceptedVersionLoadsAndAFieldAddedLaterKeepsItsDefault) {
        const GameState original = MakeState();
        ASSERT_TRUE(Service().Save("old", SaveSchema{kGame, 1, 1}, original).has_value());

        GameStateWithExtra loaded;
        loaded.lives = 3;
        const auto info = Service().Load("old", kSchema, loaded);
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(info->version, 1u);  // the caller sees that it is an older file and can fix it up
        EXPECT_EQ(loaded.lives, 3);
        EXPECT_EQ(loaded.score, original.score);
        EXPECT_EQ(loaded.title, original.title);
        EXPECT_EQ(loaded.items.size(), original.items.size());
    }

    TEST_F(SaveGameTest, FieldOfAnotherTypeIsCorrupt) {
        ASSERT_TRUE(Service().Save("slot", kSchema, MakeState()).has_value());

        ScoreAsFloatState state;  // `score` is an int32 in the file
        ExpectFailure(Service().Load("slot", kSchema, state), SaveErrorKind::Corrupt);
    }

    TEST_F(SaveGameTest, DamagedFilesAreCorruptAndNeverCrash) {
        const eastl::vector<uint8_t> valid = ValidBytes();
        ASSERT_GT(valid.size(), BinaryFormat::HeaderSize + 16);

        // A valid stream that is not a save: another root object
        BinarySerializer otherRoot;
        otherRoot.BeginObject("settings");
        otherRoot.WriteAttribute("game", kGame);
        otherRoot.WriteAttribute("version", uint32_t{2});
        otherRoot.EndObject();

        eastl::vector<uint8_t> brokenMagic = valid;
        brokenMagic[0] = 'X';
        const eastl::vector<uint8_t> halfFile(valid.begin(), valid.begin() + static_cast<ptrdiff_t>(valid.size() / 2));
        const eastl::vector<uint8_t> headerOnly(valid.begin(), valid.begin() + static_cast<ptrdiff_t>(BinaryFormat::HeaderSize));
        const eastl::vector<uint8_t> oneByteShort(valid.begin(), valid.end() - 1);
        eastl::vector<uint8_t> garbage;
        for (size_t i = 0; i < 64; ++i) {
            garbage.push_back(static_cast<uint8_t>(i * 37 + 11));
        }

        struct Case {
            const char* name;
            eastl::vector<uint8_t> bytes;
        };
        const Case cases[] = {
            {"empty file", {}},
            {"64 bytes of garbage", garbage},
            {"broken magic", brokenMagic},
            {"cut in half", halfFile},
            {"header only", headerOnly},
            {"one byte short", oneByteShort},
            {"another root object", otherRoot.GetData()},
            {"no state", EnvelopeBytes(true, true, false, false)},
            {"no version", EnvelopeBytes(true, false, false, true)},
            {"no game", EnvelopeBytes(false, true, false, true)},
            {"version of the wrong type", EnvelopeBytes(true, true, true, true)},
        };

        for (const Case& testCase : cases) {
            SCOPED_TRACE(testCase.name);
            ASSERT_TRUE(Service().WriteSlot("bad", testCase.bytes).has_value());

            GameState state = MakeUntouchedState();
            ExpectFailure(Service().Load("bad", kSchema, state), SaveErrorKind::Corrupt);
            ExpectUntouched(state);
        }
    }

    TEST_F(SaveGameTest, MissingSlotIsNotFound) {
        GameState state = MakeUntouchedState();
        ExpectFailure(Service().Load("nothing", kSchema, state), SaveErrorKind::NotFound);
        ExpectUntouched(state);
    }

    TEST_F(SaveGameTest, BadSlotNameAndMissingInitializeNeverCallTheCallbacks) {
        int calls = 0;
        ExpectFailure(Service().Save("../x", kSchema, [&calls](ISerializer&) { ++calls; }), SaveErrorKind::InvalidSlotName);
        ExpectFailure(Service().Load("a b", kSchema, [&calls](IDeserializer&) { ++calls; }), SaveErrorKind::InvalidSlotName);

        SaveGameService fresh(_fileSystem);
        ExpectFailure(fresh.Save("slot", kSchema, [&calls](ISerializer&) { ++calls; }), SaveErrorKind::NotInitialized);
        ExpectFailure(fresh.Load("slot", kSchema, [&calls](IDeserializer&) { ++calls; }), SaveErrorKind::NotInitialized);
        ExpectFailure(fresh.Peek("slot"), SaveErrorKind::NotInitialized);
        EXPECT_EQ(calls, 0);
    }

    TEST_F(SaveGameTest, CallbacksWriteAndReadTheStateObject) {
        const auto saved = Service().Save("slot", kSchema, [](ISerializer& serializer) { serializer.WriteAttribute("answer", int32_t{42}); });
        ASSERT_TRUE(saved.has_value());

        int32_t answer = 0;
        const auto info = Service().Load("slot", kSchema, [&answer](IDeserializer& deserializer) { deserializer.Read("answer", answer); });
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(answer, 42);
    }

    TEST_F(SaveGameTest, ErrorsOfTheReaderAreCorrupt) {
        ASSERT_TRUE(Service().Save("slot", kSchema, MakeState()).has_value());

        const auto info = Service().Load("slot", kSchema, [](IDeserializer& deserializer) {
            int32_t value = 0;
            deserializer.Read("not_there", value);  // Read() reports a missing entry
        });
        ExpectFailure(info, SaveErrorKind::Corrupt);
    }

    TEST_F(SaveGameTest, EveryErrorKindHasADetail) {
        // NotInitialized
        SaveGameService fresh(_fileSystem);
        ExpectFailure(fresh.Peek("a"), SaveErrorKind::NotInitialized);

        // InvalidSlotName, NotFound
        ExpectFailure(Service().Peek("a b"), SaveErrorKind::InvalidSlotName);
        ExpectFailure(Service().Peek("a"), SaveErrorKind::NotFound);

        // WriteFailed: a folder where the file should go
        std::filesystem::create_directories(_saves / "blocked.save");
        ExpectFailure(Service().Save("blocked", kSchema, MakeState()), SaveErrorKind::WriteFailed);

        // ReadFailed: a folder in place of the save
        std::filesystem::create_directories(_saves / "folder.save");
        ExpectFailure(Service().Peek("folder"), SaveErrorKind::ReadFailed);

        // Corrupt, WrongGame, VersionTooNew, VersionTooOld
        ASSERT_TRUE(Service().WriteSlot("junk", eastl::vector<uint8_t>(3, 1)).has_value());
        ExpectFailure(Service().Peek("junk"), SaveErrorKind::Corrupt);

        ASSERT_TRUE(Service().Save("slot", SaveSchema{"Other", 5, 5}, MakeState()).has_value());
        GameState state;
        ExpectFailure(Service().Load("slot", kSchema, state), SaveErrorKind::WrongGame);
        ExpectFailure(Service().Load("slot", SaveSchema{"Other", 4, 1}, state), SaveErrorKind::VersionTooNew);
        ExpectFailure(Service().Load("slot", SaveSchema{"Other", 9, 6}, state), SaveErrorKind::VersionTooOld);
    }

}  // namespace BECore::Tests
