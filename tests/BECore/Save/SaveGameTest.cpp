#include "SaveGameTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <BECore/Save/SaveGame.h>
#include <BECore/Save/SaveTestSupport.h>
#include <Common/BinaryStreams.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
#include <Generated/SaveGameTest.gen.hpp>

namespace BECore::Tests {

    using namespace SaveTestData;

    namespace {

        constexpr StringView kGame = "TestGame";

        /// The game as it is now (version 2, reads from 1)
        constexpr SaveSchema kSchema{kGame, 2, 1};

        Item MakeItem(int32_t id, float weight, StringView label) {
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
            state.hero = "Hero"_intern;
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
        Vector<uint8_t> ValidBytes() {
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
        Vector<uint8_t> EnvelopeBytes(bool withGame, bool withVersion, bool versionIsText, bool withState) {
            BinarySerializer serializer;
            serializer.BeginObject("save");
            if (withGame) {
                serializer.WriteAttribute("game", kGame);
            }
            if (withVersion && versionIsText) {
                serializer.WriteAttribute("version", StringView("two"));
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

        /// The state of the small saves below: one of each kind of entry the envelope's `state` holds
        struct SmallState {
            int32_t answer = 0;
            String text;
            PoolString pooled;
            int32_t x = 0;
            int32_t y = 0;
            Vector<int32_t> ids;
        };

        void ReadSmallState(IDeserializer& deserializer, SmallState& state) {
            deserializer.Read("answer", state.answer);
            deserializer.Read("text", state.text);
            deserializer.Read("pooled", state.pooled);
            if (deserializer.BeginObject("point")) {
                deserializer.Read("x", state.x);
                deserializer.Read("y", state.y);
                deserializer.EndObject();
            }
            size_t count = 0;
            if (deserializer.BeginArray("items", "item", count)) {
                for (size_t i = 0; i < count && deserializer.BeginArrayElement(); ++i) {
                    int32_t id = 0;
                    deserializer.Read("id", id);
                    state.ids.push_back(id);
                    deserializer.EndArrayElement();
                }
                deserializer.EndArray();
            }
        }

        constexpr int32_t kSmallAnswer = 7;
        constexpr StringView kSmallText = "level three";
        constexpr StringView kSmallPooled = "Hero";

        /// A save of SmallState described as a tree, with the independent builder of BinaryStreams: the bytes depend on no writer of the engine
        Vector<BinaryStreams::Entry> SmallSaveTree(uint32_t version) {
            using namespace BinaryStreams;
            return {Object("save", {Text("game", kGame), Number<uint32_t>("version", version),
                                    Object("state", {Number<int32_t>("answer", kSmallAnswer), Text("text", kSmallText), Pooled("pooled", kSmallPooled),
                                                     Object("point", {Number<int32_t>("x", -3), Number<int32_t>("y", 4)}),
                                                     Array("items", {Element({Number<int32_t>("id", 10)}), Element({Number<int32_t>("id", 11)})})})})};
        }

        Vector<uint8_t> SmallSaveBytes(uint16_t layout, uint32_t version) {
            BinaryStreams::Options options;
            options.version = layout;
            return BinaryStreams::Build(SmallSaveTree(version), options);
        }

        void ExpectSmallState(const SmallState& state) {
            EXPECT_EQ(state.answer, kSmallAnswer);
            EXPECT_EQ(state.text, kSmallText);
            EXPECT_EQ(state.pooled, "Hero"_intern);
            EXPECT_EQ(state.x, -3);
            EXPECT_EQ(state.y, 4);
            ASSERT_EQ(state.ids.size(), 2u);
            EXPECT_EQ(state.ids[0], 10);
            EXPECT_EQ(state.ids[1], 11);
        }

        /// The same stream in the layout of version 1: names inline, a PoolString value inline as a String. For a stream the writer made
        Vector<uint8_t> AsVersion1(const Vector<uint8_t>& stream) {
            BinaryStreams::Options options;
            options.version = BinaryFormat::LegacyVersion;
            return BinaryStreams::Build(BinaryStreams::ReadTree(stream), options);
        }

    }  // namespace

    /// A service initialised on the saves folder of the test
    class SaveGameTest : public SaveFolderTest {
    protected:
        void SetUp() override {
            ASSERT_NO_FATAL_FAILURE(SaveFolderTest::SetUp());
            _service.emplace(_fileSystem);
            ASSERT_TRUE(_service->Initialize(_saves));
        }

        void TearDown() override {
            _service.reset();
            SaveFolderTest::TearDown();
        }

        SaveGameService& Service() {
            return *_service;
        }

        Optional<SaveGameService> _service;
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

    TEST_F(SaveGameTest, ASaveOfEitherLayoutLoadsAndPeeks) {
        for (const uint16_t layout : {BinaryFormat::LegacyVersion, BinaryFormat::TabledVersion, BinaryFormat::PooledVersion}) {
            SCOPED_TRACE(layout);
            ASSERT_TRUE(Service().WriteSlot("slot", SmallSaveBytes(layout, 2)).has_value());

            SmallState state;
            const auto info = Service().Load("slot", kSchema, [&state](IDeserializer& deserializer) { ReadSmallState(deserializer, state); });
            ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
            EXPECT_EQ(info->game, kGame);
            EXPECT_EQ(info->version, 2u);
            ExpectSmallState(state);

            const auto peeked = Service().Peek("slot");
            ASSERT_TRUE(peeked.has_value());
            EXPECT_EQ(peeked->game, kGame);
            EXPECT_EQ(peeked->version, 2u);
        }
    }

    TEST_F(SaveGameTest, AVersion1SaveKeptAsBytesStillLoads) {
        // The save of a small game as the engine of version 1 wrote it, byte by byte: no writer of this engine is in the fixture
        // clang-format off
        const Vector<uint8_t> bytes = {
            'B', 'E', 'B', 'N', 0x01, 0x00,                                                 // magic, version 1
            0x39, 0x00, 0x00, 0x00,                                                         // payload size: 57
            0x0D, 0x04, 0x00, 's', 'a', 'v', 'e', 0x2E, 0x00, 0x00, 0x00,                   // Object "save", size 46
            0x0C, 0x04, 0x00, 'g', 'a', 'm', 'e', 0x01, 0x00, 0x00, 0x00, 'G',              //   String "game" = "G"
            0x07, 0x07, 0x00, 'v', 'e', 'r', 's', 'i', 'o', 'n', 0x01, 0x00, 0x00, 0x00,    //   UInt32 "version" = 1
            0x0D, 0x05, 0x00, 's', 't', 'a', 't', 'e', 0x08, 0x00, 0x00, 0x00,              //   Object "state", size 8
            0x06, 0x01, 0x00, 'n', 0x05, 0x00, 0x00, 0x00,                                  //     Int32 "n" = 5
        };
        // clang-format on

        using namespace BinaryStreams;
        const Vector<Entry> tree = {Object("save", {Text("game", "G"), Number<uint32_t>("version", 1), Object("state", {Number<int32_t>("n", 5)})})};
        BinaryStreams::Options legacy;
        legacy.version = BinaryFormat::LegacyVersion;
        ASSERT_EQ(Build(tree, legacy), bytes) << "the literal and the builder describe the same file";

        ASSERT_TRUE(Service().WriteSlot("old", bytes).has_value());
        int32_t n = 0;
        const auto info = Service().Load("old", SaveSchema{"G", 1, 1}, [&n](IDeserializer& deserializer) { deserializer.Read("n", n); });
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(info->version, 1u);
        EXPECT_EQ(n, 5);

        const auto peeked = Service().Peek("old");
        ASSERT_TRUE(peeked.has_value());
        EXPECT_EQ(peeked->game, "G");
        EXPECT_EQ(peeked->version, 1u);
    }

    TEST_F(SaveGameTest, AVersion2SaveKeptAsBytesStillLoads) {
        // The save of a small game as the engine of version 2 wrote it, byte by byte: one table of names and PoolString texts
        // clang-format off
        const Vector<uint8_t> bytes = {
            'B', 'E', 'B', 'N', 0x02, 0x00,                                                 // magic, version 2
            0x78, 0x00, 0x00, 0x00,                                                         // payload size: 120
            0x07, 0x00, 0x00, 0x00,  0x1D, 0x00, 0x00, 0x00,                                // table: 7 texts, 29 bytes
            0x04, 0x00, 0x00, 0x00,  0x08, 0x00, 0x00, 0x00,  0x0C, 0x00, 0x00, 0x00,       //   ends: Hero, game, hero
            0x0D, 0x00, 0x00, 0x00,  0x11, 0x00, 0x00, 0x00,  0x16, 0x00, 0x00, 0x00,       //   n, save, state
            0x1D, 0x00, 0x00, 0x00,                                                         //   version
            'H', 'e', 'r', 'o', 'g', 'a', 'm', 'e', 'h', 'e', 'r', 'o', 'n',                //   the characters
            's', 'a', 'v', 'e', 's', 't', 'a', 't', 'e', 'v', 'e', 'r', 's', 'i', 'o', 'n',
            0x0D, 0x04, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00, 0x00,                           // Object "save", size 46
            0x0C, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 'G',                      //   String "game" = "G"
            0x07, 0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,                           //   UInt32 "version" = 1
            0x0D, 0x05, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00,                           //   Object "state", size 18
            0x06, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,                           //     Int32 "n" = 5
            0x0F, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                           //     PoolString "hero" = table[0], "Hero"
        };
        // clang-format on

        using namespace BinaryStreams;
        const Vector<Entry> tree = {Object("save", {Text("game", "G"), Number<uint32_t>("version", 1), Object("state", {Number<int32_t>("n", 5), Pooled("hero", "Hero")})})};
        BinaryStreams::Options tabled;
        tabled.version = BinaryFormat::TabledVersion;
        ASSERT_EQ(Build(tree, tabled), bytes) << "the literal and the builder describe the same file";

        ASSERT_TRUE(Service().WriteSlot("old", bytes).has_value());
        int32_t n = 0;
        PoolString hero;
        const auto info = Service().Load("old", SaveSchema{"G", 1, 1}, [&](IDeserializer& deserializer) {
            deserializer.Read("n", n);
            deserializer.Read("hero", hero);
        });
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(info->version, 1u);
        EXPECT_EQ(n, 5);
        EXPECT_EQ(hero, "Hero"_intern);
    }

    TEST_F(SaveGameTest, AVersion3SaveWithStaleHashesLoadsTheRightPoolStrings) {
        // A save never trusts a hash it holds: a stale hash, or the hashes of another hash function, change nothing
        constexpr uint64_t kStaleHash = 0x0123456789ABCDEFull;
        for (const bool otherFunction : {false, true}) {
            SCOPED_TRACE(otherFunction);
            const String text = FreshText("save_stale_hash");  // not in the pool yet: a trusted hash would file it under the wrong key
            using namespace BinaryStreams;
            const Vector<Entry> tree = {Object("save", {Text("game", kGame), Number<uint32_t>("version", 2), Object("state", {Pooled("hero", text)})})};
            BinaryStreams::Options options;
            options.hashOverrides.push_back({text, kStaleHash});
            if (otherFunction) {
                options.hashFunction = Details::PoolHashFunctionId() + 1;
            }
            ASSERT_TRUE(Service().WriteSlot("slot", Build(tree, options)).has_value());

            PoolString hero;
            const auto info = Service().Load("slot", kSchema, [&hero](IDeserializer& deserializer) { deserializer.Read("hero", hero); });
            ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
            const Optional<PoolString> found = PoolString::Find(StringView(text));
            ASSERT_TRUE(found.has_value()) << "the lookup uses the hash of the text, not the stale one";
            EXPECT_EQ(hero, *found);
            EXPECT_EQ(hero.ToStringView(), StringView(text));
        }
    }

    TEST_F(SaveGameTest, ASaveIsWrittenAsVersion3AndReplacesAVersion1Slot) {
        ASSERT_TRUE(Service().WriteSlot("slot", SmallSaveBytes(BinaryFormat::LegacyVersion, 1)).has_value());
        const auto before = Service().ReadSlot("slot");
        ASSERT_TRUE(before.has_value());
        EXPECT_EQ(BinaryFormat::Load<uint16_t>(before->data() + sizeof(BinaryFormat::Magic)), BinaryFormat::LegacyVersion);

        const GameState state = MakeState();
        ASSERT_TRUE(Service().Save("slot", kSchema, state).has_value());

        const auto after = Service().ReadSlot("slot");
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(BinaryFormat::Load<uint16_t>(after->data() + sizeof(BinaryFormat::Magic)), BinaryFormat::PooledVersion);
        EXPECT_FALSE(PathExists(_saves / "slot.save.tmp"));

        GameState loaded;
        ASSERT_TRUE(Service().Load("slot", kSchema, loaded).has_value());
        ExpectSameState(state, loaded);
    }

    TEST_F(SaveGameTest, PeekOfAVersion2SaveInternsNothingAndALoadInternsInOneBatch) {
        GameState state = MakeState();
        state.hero = PoolString::Intern(FreshText("save_peek_hero"));
        ASSERT_TRUE(Service().Save("slot", kSchema, state).has_value());

        const uint64_t batchesBefore = PoolString::GetStats().batchCalls;
        ASSERT_TRUE(Service().Peek("slot").has_value());
        EXPECT_EQ(PoolString::GetStats().batchCalls, batchesBefore) << "a Peek reads the game and the version: no PoolString";

        GameState loaded;
        ASSERT_TRUE(Service().Load("slot", kSchema, loaded).has_value());
        EXPECT_EQ(PoolString::GetStats().batchCalls, batchesBefore + 1);
        EXPECT_EQ(loaded.hero, state.hero);
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

        ASSERT_TRUE(Service().WriteSlot("junk", Vector<uint8_t>(64, 0xAB)).has_value());
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
        // A valid stream that is not a save: another root object
        BinarySerializer otherRoot;
        otherRoot.BeginObject("settings");
        otherRoot.WriteAttribute("game", kGame);
        otherRoot.WriteAttribute("version", uint32_t{2});
        otherRoot.EndObject();

        Vector<uint8_t> garbage;
        for (size_t i = 0; i < 64; ++i) {
            garbage.push_back(static_cast<uint8_t>(i * 37 + 11));
        }

        // The same damage in the layout of the writer (version 3) and in the layout of the saves of the first engine (version 1)
        for (const uint16_t layout : {BinaryFormat::PooledVersion, BinaryFormat::LegacyVersion}) {
            const auto inLayout = [layout](const Vector<uint8_t>& stream) { return layout == BinaryFormat::LegacyVersion ? AsVersion1(stream) : stream; };

            const Vector<uint8_t> valid = inLayout(ValidBytes());
            ASSERT_GT(valid.size(), BinaryFormat::HeaderSize + 16);
            Vector<uint8_t> brokenMagic = valid;
            brokenMagic[0] = 'X';
            const Vector<uint8_t> halfFile(valid.begin(), valid.begin() + static_cast<ptrdiff_t>(valid.size() / 2));
            // Version 3: no name table at all; version 1: a header that promises more than the file has
            const Vector<uint8_t> headerOnly(valid.begin(), valid.begin() + static_cast<ptrdiff_t>(BinaryFormat::HeaderSize));
            const Vector<uint8_t> oneByteShort(valid.begin(), valid.end() - 1);

            struct Case {
                const char* name;
                Vector<uint8_t> bytes;
            };
            const Case cases[] = {
                {"empty file", {}},
                {"64 bytes of garbage", garbage},
                {"broken magic", brokenMagic},
                {"cut in half", halfFile},
                {"header only", headerOnly},
                {"one byte short", oneByteShort},
                {"another root object", inLayout(otherRoot.GetData())},
                {"no state", inLayout(EnvelopeBytes(true, true, false, false))},
                {"no version", inLayout(EnvelopeBytes(true, false, false, true))},
                {"no game", inLayout(EnvelopeBytes(false, true, false, true))},
                {"version of the wrong type", inLayout(EnvelopeBytes(true, true, true, true))},
            };

            for (const Case& testCase : cases) {
                SCOPED_TRACE(Format("layout {}: {}", layout, testCase.name));
                ASSERT_TRUE(Service().WriteSlot("bad", testCase.bytes).has_value());

                GameState state = MakeUntouchedState();
                ExpectFailure(Service().Load("bad", kSchema, state), SaveErrorKind::Corrupt);
                ExpectUntouched(state);
            }
        }
    }

    TEST_F(SaveGameTest, AFileOverTheSizeLimitIsNotReadAndNotWritten) {
        // A sparse file of the size of the limit plus one: the size is checked before a byte is allocated
        ASSERT_TRUE(Service().WriteSlot("big", ValidBytes()).has_value());
        ResizeFile(_saves / "big.save", ReaderLimits::kMaxBinaryStreamBytes + 1);

        GameState state = MakeUntouchedState();
        ExpectFailure(Service().ReadSlot("big"), SaveErrorKind::ReadFailed);
        ExpectFailure(Service().Load("big", kSchema, state), SaveErrorKind::ReadFailed);
        ExpectFailure(Service().Peek("big"), SaveErrorKind::ReadFailed);
        ExpectUntouched(state);

        // The same size on the way out: the old save stays
        ASSERT_TRUE(Service().WriteSlot("kept", ValidBytes()).has_value());
        ExpectFailure(Service().WriteSlot("kept", Vector<uint8_t>(ReaderLimits::kMaxBinaryStreamBytes + 1)), SaveErrorKind::WriteFailed);
        GameState loaded;
        EXPECT_TRUE(Service().Load("kept", kSchema, loaded).has_value());
        EXPECT_FALSE(PathExists(_saves / "kept.save.tmp"));
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

    TEST_F(SaveGameTest, NamedMutableCallbacksAreCalledOnceAndKeepTheirState) {
        int32_t written = 40;
        auto write = [&written](ISerializer& serializer) mutable { serializer.WriteAttribute("answer", ++written); };
        ASSERT_TRUE(Service().Save("slot", kSchema, write).has_value());
        EXPECT_EQ(written, 41);

        int32_t answer = 0;
        int32_t calls = 0;
        auto read = [&answer, &calls](IDeserializer& deserializer) mutable {
            ++calls;
            deserializer.Read("answer", answer);
        };
        const auto info = Service().Load("slot", kSchema, read);
        ASSERT_TRUE(info.has_value()) << info.error().detail.c_str();
        EXPECT_EQ(answer, 41);
        EXPECT_EQ(calls, 1);
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
        MakeDirectory(_saves / "blocked.save");
        ExpectFailure(Service().Save("blocked", kSchema, MakeState()), SaveErrorKind::WriteFailed);

        // ReadFailed: a folder in place of the save
        MakeDirectory(_saves / "folder.save");
        ExpectFailure(Service().Peek("folder"), SaveErrorKind::ReadFailed);

        // Corrupt, WrongGame, VersionTooNew, VersionTooOld
        ASSERT_TRUE(Service().WriteSlot("junk", Vector<uint8_t>(3, 1)).has_value());
        ExpectFailure(Service().Peek("junk"), SaveErrorKind::Corrupt);

        ASSERT_TRUE(Service().Save("slot", SaveSchema{"Other", 5, 5}, MakeState()).has_value());
        GameState state;
        ExpectFailure(Service().Load("slot", kSchema, state), SaveErrorKind::WrongGame);
        ExpectFailure(Service().Load("slot", SaveSchema{"Other", 4, 1}, state), SaveErrorKind::VersionTooNew);
        ExpectFailure(Service().Load("slot", SaveSchema{"Other", 9, 6}, state), SaveErrorKind::VersionTooOld);
    }

}  // namespace BECore::Tests
