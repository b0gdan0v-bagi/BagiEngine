#include "ReflectionTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <Generated/ReflectionTest.gen.hpp>

// =========================================================================
// Compile-time checks (requires the generated ReflectionTraits): a regression breaks the build
// =========================================================================

namespace BECore::Tests {

    // HasReflection concept
    static_assert(HasReflection<TestData::Player>, "Player should have reflection");
    static_assert(HasReflection<TestData::Inventory>, "Inventory should have reflection");
    static_assert(HasReflection<TestData::GameState>, "GameState should have reflection");

    // Field counts
    static_assert(FieldCount<TestData::Player>() == 4, "Player should have 4 fields");
    static_assert(FieldCount<TestData::Inventory>() == 2, "Inventory should have 2 fields");
    static_assert(FieldCount<TestData::GameState>() == 3, "GameState should have 3 fields");

    // HasField
    static_assert(HasField<TestData::Player>("health"), "Player should have 'health' field");
    static_assert(HasField<TestData::Player>("speed"), "Player should have 'speed' field");
    static_assert(HasField<TestData::Player>("name"), "Player should have 'name' field");
    static_assert(HasField<TestData::Player>("isAlive"), "Player should have 'isAlive' field");
    static_assert(!HasField<TestData::Player>("mana"), "Player should not have 'mana' field");

    // GetFieldInfo
    static_assert(GetFieldInfo<TestData::Player, 0>().name == "health");
    static_assert(GetFieldInfo<TestData::Player, 1>().name == "speed");
    static_assert(GetFieldInfo<TestData::Player, 2>().name == "name");
    static_assert(GetFieldInfo<TestData::Player, 3>().name == "isAlive");

    // Method reflection
    static_assert(HasMethodReflection<TestData::Player>, "Player should have method reflection");
    static_assert(MethodCount<TestData::Player>() == 4, "Player should have 4 reflected methods");

    // HasMethod
    static_assert(HasMethod<TestData::Player>("TakeDamage"), "Player should have 'TakeDamage' method");
    static_assert(HasMethod<TestData::Player>("Heal"), "Player should have 'Heal' method");
    static_assert(HasMethod<TestData::Player>("IsDead"), "Player should have 'IsDead' method");
    static_assert(HasMethod<TestData::Player>("GetHealthPercent"), "Player should have 'GetHealthPercent' method");
    static_assert(!HasMethod<TestData::Player>("Fly"), "Player should not have 'Fly' method");

    // GetMethodInfo
    static_assert(GetMethodInfo<TestData::Player, 0>().name == "TakeDamage");
    static_assert(GetMethodInfo<TestData::Player, 1>().name == "Heal");

    // Binary serializers implement the same split interfaces as XML
    static_assert(std::is_base_of_v<ISerializer, BinarySerializer>);
    static_assert(std::is_base_of_v<IDeserializer, BinaryDeserializer>);
    static_assert(BinaryFormat::HeaderSize == 10, "Header: magic(4) + version(2) + payload size(4)");

}  // namespace BECore::Tests

namespace BECore {

    // =========================================================================
    // Player method implementations
    // =========================================================================

    int32_t TestData::Player::TakeDamage(int32_t amount) {
        health -= amount;
        if (health <= 0) {
            health = 0;
            isAlive = false;
        }
        return health;
    }

    void TestData::Player::Heal(int32_t amount) {
        health += amount;
        if (health > 0) {
            isAlive = true;
        }
    }

    bool TestData::Player::IsDead() const {
        return health <= 0;
    }

    float TestData::Player::GetHealthPercent() const {
        return static_cast<float>(health);  // Assuming max health is 100
    }

}  // namespace BECore

// =========================================================================
// Tests
// =========================================================================

namespace BECore::Tests {

    namespace {

        /// Saves @p value as <objectName> with one @p key entry and returns the XML text.
        template <typename T>
        eastl::string SaveToXml(StringView objectName, StringView key, const T& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject(objectName)) {
                Ser::Save(serializer, value, key);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        /// Loads the @p key entry of <objectName> from @p xml into @p out. Use through ASSERT_NO_FATAL_FAILURE.
        template <typename T>
        void LoadFromXml(const eastl::string& xml, StringView objectName, StringView key, T& out) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml)) << "Failed to load XML:\n" << xml.c_str();
            if (deserializer.BeginObject(objectName)) {
                Ser::Load(deserializer, out, key);
                deserializer.EndObject();
            }
        }

        /// Serializes a Player into <Player ...> with the given serializer settings.
        eastl::string SavePlayer(const TestData::Player& player, bool skipDefaults) {
            XmlSerializer serializer;
            serializer.SetSkipDefaults(skipDefaults);
            if (serializer.BeginObject("Player")) {
                player.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        /// Loads a Player from @p xml into @p out. Use through ASSERT_NO_FATAL_FAILURE.
        void LoadPlayer(const eastl::string& xml, TestData::Player& out) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml)) << "Failed to load XML:\n" << xml.c_str();
            if (deserializer.BeginObject("Player")) {
                out.Deserialize(deserializer);
                deserializer.EndObject();
            }
        }

        void ExpectPlayersEqual(const TestData::Player& actual, const TestData::Player& expected) {
            EXPECT_EQ(actual.health, expected.health);
            EXPECT_EQ(actual.speed, expected.speed);
            EXPECT_EQ(actual.name, expected.name);
            EXPECT_EQ(actual.isAlive, expected.isAlive);
        }

        // ---------------------------------------------------------------------
        // Binary helpers
        // ---------------------------------------------------------------------

        using ByteBuffer = eastl::vector<uint8_t>;

        /// Joins the deserializer errors into one line for assertion messages.
        eastl::string DescribeErrors(const IDeserializer& deserializer) {
            eastl::string text;
            for (const auto& error : deserializer.GetErrors()) {
                text.append(Format("[{}: {}] ", error.path.CStr(), error.errorMessage.CStr()));
            }
            return text;
        }

        /// Saves @p value as <objectName> with one @p key entry and returns the binary stream.
        template <typename T>
        ByteBuffer SaveToBinary(StringView objectName, StringView key, const T& value) {
            BinarySerializer serializer;
            if (serializer.BeginObject(objectName)) {
                Ser::Save(serializer, value, key);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// Loads the @p key entry of <objectName> from @p data into @p out. Use through ASSERT_NO_FATAL_FAILURE.
        template <typename T>
        void LoadFromBinary(const ByteBuffer& data, StringView objectName, StringView key, T& out) {
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(data)) << DescribeErrors(deserializer).c_str();
            ASSERT_TRUE(deserializer.BeginObject(objectName)) << "the object is missing from the binary stream";
            Ser::Load(deserializer, out, key);
            deserializer.EndObject();
            EXPECT_FALSE(deserializer.HasErrors()) << DescribeErrors(deserializer).c_str();
        }

        /// Binary round-trip of one value; @p loaded is pre-set by the caller to prove that loading overwrites it.
        template <typename T>
        void ExpectBinaryRoundTrip(const T& original, T loaded) {
            const ByteBuffer data = SaveToBinary("RoundTrip", "value", original);
            ASSERT_NO_FATAL_FAILURE(LoadFromBinary(data, "RoundTrip", "value", loaded));
            EXPECT_TRUE(loaded == original) << "the value changed during the binary round-trip";
        }

        ByteBuffer SavePlayerBinary(const TestData::Player& player, bool skipDefaults) {
            BinarySerializer serializer;
            serializer.SetSkipDefaults(skipDefaults);
            if (serializer.BeginObject("Player")) {
                player.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// Loads a Player from @p data into @p out. Use through ASSERT_NO_FATAL_FAILURE.
        void LoadPlayerBinary(const ByteBuffer& data, TestData::Player& out) {
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(data)) << DescribeErrors(deserializer).c_str();
            ASSERT_TRUE(deserializer.BeginObject("Player")) << "Player is missing from the binary stream";
            out.Deserialize(deserializer);
            deserializer.EndObject();
            EXPECT_FALSE(deserializer.HasErrors()) << DescribeErrors(deserializer).c_str();
        }

        /// Header + @p payload, with a consistent payload size, for hand-made (valid or corrupt) streams.
        ByteBuffer MakeStream(std::initializer_list<uint8_t> payload) {
            ByteBuffer stream;
            stream.insert(stream.end(), BinaryFormat::Magic, BinaryFormat::Magic + sizeof(BinaryFormat::Magic));
            const uint16_t version = BinaryFormat::CurrentVersion;
            stream.insert(stream.end(), reinterpret_cast<const uint8_t*>(&version), reinterpret_cast<const uint8_t*>(&version) + sizeof(version));
            const auto payloadSize = static_cast<uint32_t>(payload.size());
            stream.insert(stream.end(), reinterpret_cast<const uint8_t*>(&payloadSize), reinterpret_cast<const uint8_t*>(&payloadSize) + sizeof(payloadSize));
            stream.insert(stream.end(), payload.begin(), payload.end());
            return stream;
        }

        void PatchU32(ByteBuffer& data, size_t offset, uint32_t value) {
            std::memcpy(data.data() + offset, &value, sizeof(value));
        }

        /// A stream that uses nested objects, arrays of objects, maps, strings and numbers.
        ByteBuffer MakeRichDocument() {
            TestData::GameState state;
            state.player.health = 64;
            state.player.speed = 2.5f;
            state.player.name = PoolString::Intern("Rich");
            state.inventory.gold = 900;
            state.inventory.itemCount = 3;
            state.level = 4;

            eastl::vector<TestData::Player> players(3);
            players[0].name = PoolString::Intern("First");
            players[1].health = 7;
            players[2].isAlive = false;

            const eastl::map<eastl::string, int32_t> scores = {{"alpha", 1}, {"bravo", 2}, {"charlie", 3}};

            BinarySerializer serializer;
            if (serializer.BeginObject("Doc")) {
                Ser::Save(serializer, state, "state");
                Ser::Save(serializer, players, "players");
                Ser::Save(serializer, scores, "scores");
                serializer.Write("title", StringView("a title"));
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// Reads everything MakeRichDocument wrote. Used on mutated streams: the result does not matter, it must not crash.
        void ReadRichDocument(BinaryDeserializer& deserializer) {
            if (!deserializer.BeginObject("Doc")) {
                return;
            }
            TestData::GameState state;
            Ser::Load(deserializer, state, "state");
            eastl::vector<TestData::Player> players;
            Ser::Load(deserializer, players, "players");
            eastl::map<eastl::string, int32_t> scores;
            Ser::Load(deserializer, scores, "scores");
            eastl::string title;
            deserializer.ReadAttribute("title", title);
            deserializer.EndObject();
        }

        /// Write() and WriteAttribute() must produce entries that Read() and ReadAttribute() both find.
        template <typename T>
        void ExpectScalarRoundTrip(T value) {
            BinarySerializer serializer;
            serializer.Write("viaWrite", value);
            serializer.WriteAttribute("viaAttribute", value);

            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData())) << DescribeErrors(deserializer).c_str();

            for (const char* key : {"viaWrite", "viaAttribute"}) {
                SCOPED_TRACE(key);

                T viaRead{};
                ASSERT_TRUE(deserializer.Read(key, viaRead));
                // Compared as bytes so that NaN and -0.0 count as exact matches
                EXPECT_EQ(std::memcmp(&viaRead, &value, sizeof(T)), 0) << "Read()";

                T viaReadAttribute{};
                ASSERT_TRUE(deserializer.ReadAttribute(key, viaReadAttribute));
                EXPECT_EQ(std::memcmp(&viaReadAttribute, &value, sizeof(T)), 0) << "ReadAttribute()";
            }
            EXPECT_FALSE(deserializer.HasErrors()) << DescribeErrors(deserializer).c_str();
        }

    }  // namespace

    TEST(ReflectionTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(ReflectionTest, FieldAccess) {
        TestData::Player player;
        player.health = 42;
        player.speed = 3.14f;
        player.name = PoolString::Intern("TestPlayer");
        player.isAlive = false;

        int fieldCount = 0;
        ForEachField(player, [&](StringView, auto&) { fieldCount++; });

        EXPECT_EQ(fieldCount, 4) << "ForEachField should visit every reflected field";
    }

    TEST(ReflectionTest, XmlSerialization) {
        TestData::Player original;
        original.health = 75;
        original.speed = 10.5f;
        original.name = PoolString::Intern("Hero");
        original.isAlive = true;

        const eastl::string xml = SavePlayer(original, false);

        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;
        ASSERT_NO_FATAL_FAILURE(LoadPlayer(xml, loaded));

        ExpectPlayersEqual(loaded, original);
    }

    // =========================================================================
    // Binary serialization
    // =========================================================================

    TEST(ReflectionTest, BinarySerialization) {
        TestData::Player original;
        original.health = 75;
        original.speed = 10.5f;
        original.name = PoolString::Intern("Hero");
        original.isAlive = true;

        const ByteBuffer data = SavePlayerBinary(original, false);

        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;
        ASSERT_NO_FATAL_FAILURE(LoadPlayerBinary(data, loaded));

        ExpectPlayersEqual(loaded, original);
    }

    TEST(ReflectionTest, BinaryNestedObjects) {
        TestData::GameState original;
        original.player.health = 12;
        original.player.speed = 7.25f;
        original.player.name = PoolString::Intern("Nested");
        original.player.isAlive = false;
        original.inventory.gold = 1500;
        original.inventory.itemCount = 7;
        original.level = 9;

        const ByteBuffer data = SaveToBinary("Save", "state", original);

        TestData::GameState loaded;
        loaded.player.health = 0;
        loaded.player.speed = 0.0f;
        loaded.player.isAlive = true;
        loaded.inventory.gold = 0;
        loaded.inventory.itemCount = 0;
        loaded.level = 0;
        ASSERT_NO_FATAL_FAILURE(LoadFromBinary(data, "Save", "state", loaded));

        ExpectPlayersEqual(loaded.player, original.player);
        EXPECT_EQ(loaded.inventory.gold, original.inventory.gold);
        EXPECT_EQ(loaded.inventory.itemCount, original.inventory.itemCount);
        EXPECT_EQ(loaded.level, original.level);
    }

    TEST(ReflectionTest, BinarySkipDefaults) {
        TestData::Player player;
        player.health = 0;
        player.speed = 0.0f;
        player.name = PoolString{};
        player.isAlive = false;

        const ByteBuffer full = SavePlayerBinary(player, false);
        const ByteBuffer skipped = SavePlayerBinary(player, true);

        EXPECT_LT(skipped.size(), full.size()) << "skip-defaults binary should be smaller";

        // The defaults must come back as the same values
        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;
        ASSERT_NO_FATAL_FAILURE(LoadPlayerBinary(skipped, loaded));
        ExpectPlayersEqual(loaded, player);

        // Non-default values must still be written
        TestData::Player hero;
        hero.health = 75;
        hero.speed = 10.5f;
        hero.name = PoolString::Intern("Hero");
        hero.isAlive = true;

        const ByteBuffer heroData = SavePlayerBinary(hero, true);

        TestData::Player heroLoaded;
        ASSERT_NO_FATAL_FAILURE(LoadPlayerBinary(heroData, heroLoaded));
        ExpectPlayersEqual(heroLoaded, hero);
    }

    TEST(ReflectionTest, BinaryIsSmallerThanXml) {
        TestData::GameState state;
        state.player.health = 80;
        state.player.speed = 4.5f;
        state.player.name = PoolString::Intern("Compact");
        state.inventory.gold = 300;
        state.inventory.itemCount = 5;
        state.level = 3;

        const ByteBuffer binary = SaveToBinary("Save", "state", state);
        const eastl::string xml = SaveToXml("Save", "state", state);

        EXPECT_LT(binary.size(), xml.size()) << "binary " << binary.size() << " bytes, XML " << xml.size() << " bytes";
    }

    TEST(ReflectionTest, BinaryContainers) {
        {
            SCOPED_TRACE("eastl::map");
            const eastl::map<eastl::string, int32_t> original = {{"charlie", 3}, {"alpha", 1}, {"bravo", 2}};
            ExpectBinaryRoundTrip(original, eastl::map<eastl::string, int32_t>{{"stale", 99}});
        }
        {
            SCOPED_TRACE("eastl::unordered_map");
            const eastl::unordered_map<eastl::string, int32_t> original = {{"zebra", 26}, {"apple", 1}, {"mango", 13}};
            ExpectBinaryRoundTrip(original, eastl::unordered_map<eastl::string, int32_t>{{"stale", 99}});
        }
        {
            SCOPED_TRACE("eastl::fixed_vector");
            eastl::fixed_vector<int32_t, 8> original;
            original.push_back(10);
            original.push_back(20);
            original.push_back(30);
            ExpectBinaryRoundTrip(original, eastl::fixed_vector<int32_t, 8>{99});
        }
        {
            SCOPED_TRACE("eastl::vector of strings");
            const eastl::vector<eastl::string> original = {"one", "", "three"};
            ExpectBinaryRoundTrip(original, eastl::vector<eastl::string>{"stale"});
        }
        {
            SCOPED_TRACE("eastl::array");
            const eastl::array<float, 3> original = {1.0f, 2.5f, 3.75f};
            ExpectBinaryRoundTrip(original, eastl::array<float, 3>{});
        }
        {
            SCOPED_TRACE("std::array");
            const std::array<float, 3> original = {4.0f, 5.5f, 6.25f};
            ExpectBinaryRoundTrip(original, std::array<float, 3>{});
        }
        {
            SCOPED_TRACE("eastl::pair");
            const eastl::pair<int32_t, eastl::string> original = {42, eastl::string("hello")};
            ExpectBinaryRoundTrip(original, eastl::pair<int32_t, eastl::string>{0, eastl::string{}});
        }
        {
            SCOPED_TRACE("std::pair");
            const std::pair<int32_t, eastl::string> original = {99, eastl::string("world")};
            ExpectBinaryRoundTrip(original, std::pair<int32_t, eastl::string>{0, eastl::string{}});
        }
        {
            SCOPED_TRACE("eastl::optional with value");
            ExpectBinaryRoundTrip(Optional<int32_t>(77), Optional<int32_t>{});
        }
        {
            SCOPED_TRACE("eastl::optional without value: the key is absent and loading resets the target");
            ExpectBinaryRoundTrip(Optional<int32_t>{}, Optional<int32_t>(55));
        }
        {
            SCOPED_TRACE("std::optional with value");
            ExpectBinaryRoundTrip(Optional<int32_t>(123), Optional<int32_t>{});
        }
        {
            SCOPED_TRACE("empty eastl::vector");
            ExpectBinaryRoundTrip(eastl::vector<int32_t>{}, eastl::vector<int32_t>{1, 2, 3});
        }
    }

    TEST(ReflectionTest, BinaryArrayShorterThanTarget) {
        const eastl::array<int32_t, 2> small = {7, 8};
        const ByteBuffer data = SaveToBinary("PartialArr", "values", small);

        eastl::array<int32_t, 4> loaded = {99, 99, 99, 99};
        ASSERT_NO_FATAL_FAILURE(LoadFromBinary(data, "PartialArr", "values", loaded));

        EXPECT_EQ(loaded[0], 7);
        EXPECT_EQ(loaded[1], 8);
        EXPECT_EQ(loaded[2], 0) << "elements missing from the stream are value-initialized";
        EXPECT_EQ(loaded[3], 0) << "elements missing from the stream are value-initialized";
    }

    TEST(ReflectionTest, BinaryReflectedObjectsInContainers) {
        // Array of reflected objects
        {
            SCOPED_TRACE("eastl::vector<Player>");
            eastl::vector<TestData::Player> original(3);
            original[0].name = PoolString::Intern("First");
            original[1].health = 7;
            original[1].speed = 1.5f;
            original[2].isAlive = false;

            const ByteBuffer data = SaveToBinary("Party", "players", original);

            eastl::vector<TestData::Player> loaded(1);
            ASSERT_NO_FATAL_FAILURE(LoadFromBinary(data, "Party", "players", loaded));

            ASSERT_EQ(loaded.size(), original.size());
            for (size_t i = 0; i < original.size(); ++i) {
                SCOPED_TRACE(i);
                ExpectPlayersEqual(loaded[i], original[i]);
            }
        }

        // Map whose values are reflected objects: "key" and the object's own fields share one element
        {
            SCOPED_TRACE("eastl::map<string, Player>");
            eastl::map<eastl::string, TestData::Player> original;
            original["bob"].health = 10;
            original["bob"].name = PoolString::Intern("Bob");
            original["alice"].health = 20;
            original["alice"].isAlive = false;

            const ByteBuffer data = SaveToBinary("Roster", "players", original);

            eastl::map<eastl::string, TestData::Player> loaded;
            ASSERT_NO_FATAL_FAILURE(LoadFromBinary(data, "Roster", "players", loaded));

            ASSERT_EQ(loaded.size(), original.size());
            for (const auto& [name, player] : original) {
                const auto it = loaded.find(name);
                ASSERT_TRUE(it != loaded.end()) << "entry '" << name.c_str() << "' is missing";
                ExpectPlayersEqual(it->second, player);
            }
        }
    }

    TEST(ReflectionTest, BinaryLargeArray) {
        eastl::vector<int32_t> original;
        original.reserve(20000);
        for (int32_t i = 0; i < 20000; ++i) {
            original.push_back(i * 3 - 1000);
        }
        ExpectBinaryRoundTrip(original, eastl::vector<int32_t>{});
    }

    TEST(ReflectionTest, BinaryScalarTypes) {
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<bool>(true));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<bool>(false));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int8_t>(std::numeric_limits<int8_t>::min()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int8_t>(std::numeric_limits<int8_t>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<uint8_t>(std::numeric_limits<uint8_t>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int16_t>(std::numeric_limits<int16_t>::min()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<uint16_t>(std::numeric_limits<uint16_t>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int32_t>(std::numeric_limits<int32_t>::min()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int32_t>(std::numeric_limits<int32_t>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<uint32_t>(std::numeric_limits<uint32_t>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int64_t>(std::numeric_limits<int64_t>::min()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<int64_t>(std::numeric_limits<int64_t>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<uint64_t>(std::numeric_limits<uint64_t>::max()));
    }

    TEST(ReflectionTest, BinaryFloatingPointIsBitExact) {
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<float>(3.14159274f));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<float>(-0.0f));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<float>(std::numeric_limits<float>::max()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<float>(std::numeric_limits<float>::denorm_min()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<float>(std::numeric_limits<float>::infinity()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<float>(std::numeric_limits<float>::quiet_NaN()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<double>(2.718281828459045));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<double>(-0.0));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<double>(std::numeric_limits<double>::lowest()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<double>(-std::numeric_limits<double>::infinity()));
        ASSERT_NO_FATAL_FAILURE(ExpectScalarRoundTrip<double>(std::numeric_limits<double>::quiet_NaN()));
    }

    TEST(ReflectionTest, BinaryStrings) {
        const eastl::string longText(100000, 'x');
        const char embeddedNul[] = {'a', '\0', 'b'};

        BinarySerializer serializer;
        serializer.Write("empty", StringView(""));
        serializer.Write("utf8", StringView("h\xC3\xA9llo w\xC3\xB6rld \xE2\x9C\x93"));
        serializer.Write("embeddedNul", StringView(embeddedNul, sizeof(embeddedNul)));
        serializer.WriteAttribute("long", StringView(longText));
        serializer.Write("pool", PoolString::Intern("Hero"));
        serializer.WriteAttribute("emptyPool", PoolString{});

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData())) << DescribeErrors(deserializer).c_str();

        eastl::string text = "stale";
        ASSERT_TRUE(deserializer.Read("empty", text));
        EXPECT_TRUE(text.empty());

        ASSERT_TRUE(deserializer.Read("utf8", text));
        EXPECT_EQ(text, eastl::string("h\xC3\xA9llo w\xC3\xB6rld \xE2\x9C\x93"));

        ASSERT_TRUE(deserializer.ReadAttribute("embeddedNul", text));
        EXPECT_EQ(text.size(), 3u) << "strings are length-prefixed, an embedded NUL does not end them";
        EXPECT_EQ(text[0], 'a');
        EXPECT_EQ(text[1], '\0');
        EXPECT_EQ(text[2], 'b');

        ASSERT_TRUE(deserializer.Read("long", text));
        EXPECT_EQ(text, longText);

        PoolString pool;
        ASSERT_TRUE(deserializer.ReadAttribute("pool", pool));
        EXPECT_EQ(pool, PoolString::Intern("Hero"));

        PoolString emptyPool = PoolString::Intern("stale");
        ASSERT_TRUE(deserializer.Read("emptyPool", emptyPool));
        EXPECT_TRUE(emptyPool.Empty());

        EXPECT_FALSE(deserializer.HasErrors()) << DescribeErrors(deserializer).c_str();
    }

    TEST(ReflectionTest, BinaryWireFormat) {
        // The bytes are part of the file format: this test breaks when the layout changes by accident.
        {
            SCOPED_TRACE("scalars and an object");
            BinarySerializer serializer;
            serializer.Write("a", int32_t{1});
            if (serializer.BeginObject("o")) {
                serializer.WriteAttribute("b", true);
                serializer.EndObject();
            }

            // clang-format off
            const ByteBuffer expected = {
                'B', 'E', 'B', 'N', 0x01, 0x00,                                                 // magic, version 1
                0x15, 0x00, 0x00, 0x00,                                                         // payload size: 21
                0x06, 0x01, 0x00, 'a', 0x01, 0x00, 0x00, 0x00,                                  // Int32 "a" = 1
                0x0D, 0x01, 0x00, 'o', 0x05, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 'b', 0x01,     // Object "o" { Bool "b" = true }
            };
            // clang-format on
            EXPECT_TRUE(serializer.GetData() == expected) << "unexpected bytes, size " << serializer.GetData().size();
        }
        {
            SCOPED_TRACE("array with a filled and an empty element");
            BinarySerializer serializer;
            size_t count = 2;
            if (serializer.BeginArray("x", "i", count)) {
                if (serializer.BeginArrayElement()) {
                    serializer.WriteAttribute("v", uint8_t{7});
                    serializer.EndArrayElement();
                }
                if (serializer.BeginArrayElement()) {
                    serializer.EndArrayElement();
                }
                serializer.EndArray();
            }

            // clang-format off
            const ByteBuffer expected = {
                'B', 'E', 'B', 'N', 0x01, 0x00,                                                 // magic, version 1
                0x19, 0x00, 0x00, 0x00,                                                         // payload size: 25
                0x0E, 0x01, 0x00, 'x', 0x11, 0x00, 0x00, 0x00,                                  // Array "x", size 17
                0x02, 0x00, 0x00, 0x00,                                                         // 2 elements
                0x05, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 'v', 0x07,                            // element 0 { UInt8 "v" = 7 }
                0x00, 0x00, 0x00, 0x00,                                                         // element 1 {}
            };
            // clang-format on
            EXPECT_TRUE(serializer.GetData() == expected) << "unexpected bytes, size " << serializer.GetData().size();
        }
    }

    TEST(ReflectionTest, BinaryEntriesAreMatchedByName) {
        BinarySerializer serializer;
        serializer.Write("first", int32_t{1});
        serializer.Write("second", int32_t{2});
        serializer.Write("third", int32_t{3});

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

        // Reading in a different order than writing, and skipping an entry, works because entries are looked up by name
        int32_t third = 0;
        int32_t first = 0;
        ASSERT_TRUE(deserializer.Read("third", third));
        ASSERT_TRUE(deserializer.Read("first", first));
        EXPECT_EQ(third, 3);
        EXPECT_EQ(first, 1);
        EXPECT_FALSE(deserializer.HasErrors());
    }

    TEST(ReflectionTest, BinaryDuplicateNameLastWriteWins) {
        BinarySerializer serializer;
        serializer.Write("x", int32_t{1});
        serializer.Write("x", int32_t{2});
        if (serializer.BeginObject("o")) {
            serializer.Write("a", int32_t{10});
            serializer.EndObject();
        }
        if (serializer.BeginObject("o")) {
            serializer.Write("a", int32_t{20});
            serializer.EndObject();
        }

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

        int32_t x = 0;
        ASSERT_TRUE(deserializer.Read("x", x));
        EXPECT_EQ(x, 2);

        ASSERT_TRUE(deserializer.BeginObject("o"));
        int32_t a = 0;
        ASSERT_TRUE(deserializer.Read("a", a));
        EXPECT_EQ(a, 20);
        deserializer.EndObject();
    }

    TEST(ReflectionTest, BinaryScalarAndObjectMayShareAName) {
        BinarySerializer serializer;
        serializer.Write("x", int32_t{5});
        if (serializer.BeginObject("x")) {
            serializer.Write("inner", int32_t{6});
            serializer.EndObject();
        }

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

        int32_t scalar = 0;
        ASSERT_TRUE(deserializer.Read("x", scalar));
        EXPECT_EQ(scalar, 5) << "scalar lookups skip objects";

        ASSERT_TRUE(deserializer.BeginObject("x"));
        int32_t inner = 0;
        ASSERT_TRUE(deserializer.Read("inner", inner));
        EXPECT_EQ(inner, 6);
        deserializer.EndObject();
    }

    TEST(ReflectionTest, BinaryArrayCountIsTheNumberOfElementsWritten) {
        // Callers may announce a count and then skip elements (null pointers in a factory-backed vector)
        BinarySerializer serializer;
        size_t announcedCount = 5;
        ASSERT_TRUE(serializer.BeginArray("items", "item", announcedCount));
        for (int32_t i = 0; i < 2; ++i) {
            ASSERT_TRUE(serializer.BeginArrayElement());
            serializer.WriteAttribute("value", i + 100);
            serializer.EndArrayElement();
        }
        serializer.EndArray();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

        size_t count = 0;
        ASSERT_TRUE(deserializer.BeginArray("items", "item", count));
        EXPECT_EQ(count, 2u);

        for (int32_t i = 0; i < 2; ++i) {
            ASSERT_TRUE(deserializer.BeginArrayElement());
            int32_t value = 0;
            ASSERT_TRUE(deserializer.ReadAttribute("value", value));
            EXPECT_EQ(value, i + 100);
            deserializer.EndArrayElement();
        }
        EXPECT_FALSE(deserializer.BeginArrayElement()) << "there are only two elements";
        deserializer.EndArray();
    }

    TEST(ReflectionTest, BinaryErrorReporting) {
        BinarySerializer serializer;
        if (serializer.BeginObject("outer")) {
            if (serializer.BeginObject("inner")) {
                serializer.Write("present", int32_t{1});
                serializer.EndObject();
            }
            size_t count = 2;
            if (serializer.BeginArray("items", "item", count)) {
                for (int32_t i = 0; i < 2; ++i) {
                    if (serializer.BeginArrayElement()) {
                        serializer.WriteAttribute("value", i);
                        serializer.EndArrayElement();
                    }
                }
                serializer.EndArray();
            }
            serializer.EndObject();
        }

        // Read() reports a missing entry with its full path
        {
            SCOPED_TRACE("missing entry inside nested objects");
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

            ASSERT_TRUE(deserializer.BeginObject("outer"));
            ASSERT_TRUE(deserializer.BeginObject("inner"));
            int32_t missing = 0;
            EXPECT_FALSE(deserializer.Read("nonExistentField", missing));
            deserializer.EndObject();
            deserializer.EndObject();

            ASSERT_TRUE(deserializer.HasErrors());
            ASSERT_EQ(deserializer.GetErrors().size(), 1u);
            EXPECT_STREQ(deserializer.GetErrors()[0].path.CStr(), "outer.inner.nonExistentField");
            EXPECT_STREQ(deserializer.GetErrors()[0].errorMessage.CStr(), "Element not found");
        }

        // Array elements show their index in the path
        {
            SCOPED_TRACE("missing entry inside an array element");
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

            ASSERT_TRUE(deserializer.BeginObject("outer"));
            size_t count = 0;
            ASSERT_TRUE(deserializer.BeginArray("items", "item", count));
            ASSERT_EQ(count, 2u);
            for (size_t i = 0; i < count; ++i) {
                ASSERT_TRUE(deserializer.BeginArrayElement());
                if (i == 1) {
                    int32_t missing = 0;
                    EXPECT_FALSE(deserializer.Read("missing", missing));
                }
                deserializer.EndArrayElement();
            }
            deserializer.EndArray();
            deserializer.EndObject();

            ASSERT_EQ(deserializer.GetErrors().size(), 1u);
            EXPECT_STREQ(deserializer.GetErrors()[0].path.CStr(), "outer.items[1].missing");
        }

        // ReadAttribute() and BeginObject() stay silent: the field may be optional
        {
            SCOPED_TRACE("optional lookups");
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

            int32_t value = 42;
            EXPECT_FALSE(deserializer.ReadAttribute("nope", value));
            EXPECT_EQ(value, 42) << "a failed read leaves the target untouched";
            EXPECT_FALSE(deserializer.BeginObject("nope"));
            size_t count = 0;
            EXPECT_FALSE(deserializer.BeginArray("nope", "item", count));
            EXPECT_FALSE(deserializer.HasErrors());
        }
    }

    TEST(ReflectionTest, BinaryTypeMismatchIsReported) {
        BinarySerializer serializer;
        serializer.Write("number", int32_t{7});
        serializer.Write("text", StringView("seven"));

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

        float asFloat = 1.0f;
        EXPECT_FALSE(deserializer.Read("number", asFloat));
        EXPECT_EQ(asFloat, 1.0f) << "a failed read leaves the target untouched";

        int64_t asInt64 = 1;
        EXPECT_FALSE(deserializer.ReadAttribute("number", asInt64)) << "widening is not implicit";

        int32_t asInt32 = 1;
        EXPECT_FALSE(deserializer.Read("text", asInt32));

        eastl::string asString = "unchanged";
        EXPECT_FALSE(deserializer.ReadAttribute("number", asString));
        EXPECT_EQ(asString, eastl::string("unchanged"));

        // Unlike a missing optional entry, a mismatch means the data is not what the reader expects: always an error
        ASSERT_EQ(deserializer.GetErrors().size(), 4u) << DescribeErrors(deserializer).c_str();
        for (const auto& error : deserializer.GetErrors()) {
            EXPECT_STREQ(error.errorMessage.CStr(), "Type mismatch");
        }

        // The entries themselves are intact
        int32_t number = 0;
        EXPECT_TRUE(deserializer.Read("number", number));
        EXPECT_EQ(number, 7);
    }

    TEST(ReflectionTest, BinaryErrorsCanBeCleared) {
        BinarySerializer serializer;
        serializer.Write("a", int32_t{1});

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));

        int32_t value = 0;
        EXPECT_FALSE(deserializer.Read("missing", value));
        EXPECT_TRUE(deserializer.HasErrors());

        deserializer.ClearErrors();
        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_TRUE(deserializer.GetErrors().empty());
    }

    TEST(ReflectionTest, BinarySerializerRejectsMisplacedCalls) {
        // BeginArrayElement is only valid directly inside an array
        BinarySerializer serializer;
        EXPECT_FALSE(serializer.BeginArrayElement());
        ASSERT_TRUE(serializer.BeginObject("o"));
        EXPECT_FALSE(serializer.BeginArrayElement());
        serializer.EndObject();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData())) << "the misplaced calls must not corrupt the stream";
        EXPECT_TRUE(deserializer.BeginObject("o"));
    }

    TEST(ReflectionTest, BinaryHeaderOnlyStreamIsValid) {
        const ByteBuffer empty = MakeStream({});

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(empty)) << DescribeErrors(deserializer).c_str();
        EXPECT_FALSE(deserializer.BeginObject("anything"));
        EXPECT_FALSE(deserializer.HasErrors());

        const BinarySerializer untouched;
        BinaryDeserializer fromUntouched;
        EXPECT_TRUE(fromUntouched.LoadFromBuffer(untouched.GetData())) << "a serializer that wrote nothing still produces a loadable stream";
    }

    TEST(ReflectionTest, BinaryRejectsTruncatedStreams) {
        const ByteBuffer data = MakeRichDocument();
        ASSERT_GT(data.size(), BinaryFormat::HeaderSize);

        {
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(data)) << "the complete stream must load: " << DescribeErrors(deserializer).c_str();
        }

        // Every shorter prefix is invalid (the header carries the payload size), and none of them may crash
        for (size_t length = 0; length < data.size(); ++length) {
            SCOPED_TRACE(length);
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(data.data(), length));
            EXPECT_TRUE(deserializer.HasErrors());
            EXPECT_FALSE(deserializer.BeginObject("Doc")) << "a rejected stream exposes nothing";
        }
    }

    TEST(ReflectionTest, BinaryRejectsBadHeaders) {
        const ByteBuffer good = MakeRichDocument();
        {
            // Positive control: every case below differs from this stream by one change only
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromBuffer(good)) << DescribeErrors(deserializer).c_str();
        }

        {
            SCOPED_TRACE("bad magic");
            ByteBuffer data = good;
            data[0] = 'X';
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(data));
            EXPECT_TRUE(deserializer.HasErrors());
        }
        {
            SCOPED_TRACE("unsupported version");
            ByteBuffer data = good;
            data[4] = static_cast<uint8_t>(BinaryFormat::CurrentVersion + 1);
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(data));
            EXPECT_TRUE(deserializer.HasErrors());
        }
        {
            SCOPED_TRACE("payload size larger than the data");
            ByteBuffer data = good;
            PatchU32(data, BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(data.size()));
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(data));
        }
        {
            SCOPED_TRACE("payload size smaller than the data");
            ByteBuffer data = good;
            PatchU32(data, BinaryFormat::PayloadSizeOffset, 1);
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(data));
        }
        {
            SCOPED_TRACE("trailing byte");
            ByteBuffer data = good;
            data.push_back(0);
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(data));
        }
        {
            SCOPED_TRACE("no data");
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(nullptr, 0));
            EXPECT_TRUE(deserializer.HasErrors());
        }
    }

    TEST(ReflectionTest, BinaryRejectsCorruptStructure) {
        // Streams with a valid header and a payload that is broken in one specific way
        struct Case {
            const char* description;
            ByteBuffer data;
        };

        const Case cases[] = {
            {"unknown tag", MakeStream({0xFF, 0x01, 0x00, 'a', 0x00, 0x00, 0x00, 0x00})},
            {"tag zero", MakeStream({0x00, 0x01, 0x00, 'a', 0x00, 0x00, 0x00, 0x00})},
            {"entry header cut short", MakeStream({0x06})},
            {"name longer than the data", MakeStream({0x06, 0xFF, 0xFF, 'a'})},
            {"scalar payload cut short", MakeStream({0x06, 0x01, 0x00, 'a', 0x01, 0x00})},
            {"string longer than the data", MakeStream({0x0C, 0x01, 0x00, 's', 0xFF, 0xFF, 0xFF, 0x7F})},
            {"object larger than the data", MakeStream({0x0D, 0x01, 0x00, 'o', 0x10, 0x00, 0x00, 0x00})},
            {"object holding a broken entry", MakeStream({0x0D, 0x01, 0x00, 'o', 0x01, 0x00, 0x00, 0x00, 0x06})},
            {"array without a count", MakeStream({0x0E, 0x01, 0x00, 'x', 0x00, 0x00, 0x00, 0x00})},
            {"array count larger than the data", MakeStream({0x0E, 0x01, 0x00, 'x', 0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x7F})},
            {"array element larger than the array", MakeStream({0x0E, 0x01, 0x00, 'x', 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00})},
            {"array with bytes after its elements", MakeStream({0x0E, 0x01, 0x00, 'x', 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA})},
        };

        for (const Case& testCase : cases) {
            SCOPED_TRACE(testCase.description);
            BinaryDeserializer deserializer;
            EXPECT_FALSE(deserializer.LoadFromBuffer(testCase.data));
            EXPECT_TRUE(deserializer.HasErrors());
        }

        // Positive controls: the same shapes, well-formed, load
        const ByteBuffer validScalar = MakeStream({0x06, 0x01, 0x00, 'a', 0x01, 0x00, 0x00, 0x00});
        const ByteBuffer validObject = MakeStream({0x0D, 0x01, 0x00, 'o', 0x08, 0x00, 0x00, 0x00, 0x06, 0x01, 0x00, 'a', 0x01, 0x00, 0x00, 0x00});
        const ByteBuffer validArray = MakeStream({0x0E, 0x01, 0x00, 'x', 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
        for (const ByteBuffer* data : {&validScalar, &validObject, &validArray}) {
            BinaryDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromBuffer(*data)) << DescribeErrors(deserializer).c_str();
        }
    }

    TEST(ReflectionTest, BinaryLimitsNestingDepth) {
        const auto saveNested = [](size_t depth) {
            BinarySerializer serializer;
            for (size_t i = 0; i < depth; ++i) {
                serializer.BeginObject("n");
            }
            for (size_t i = 0; i < depth; ++i) {
                serializer.EndObject();
            }
            return serializer.GetData();
        };

        BinaryDeserializer atLimit;
        EXPECT_TRUE(atLimit.LoadFromBuffer(saveNested(BinaryFormat::MaxDepth))) << DescribeErrors(atLimit).c_str();

        BinaryDeserializer beyondLimit;
        EXPECT_FALSE(beyondLimit.LoadFromBuffer(saveNested(BinaryFormat::MaxDepth + 1)));

        // A hostile depth must be rejected without exhausting the stack
        BinaryDeserializer hostile;
        EXPECT_FALSE(hostile.LoadFromBuffer(saveNested(20000)));
    }

    TEST(ReflectionTest, BinaryByteMutationsNeverCrash) {
        const ByteBuffer good = MakeRichDocument();

        size_t rejected = 0;
        size_t accepted = 0;
        for (size_t position = 0; position < good.size(); ++position) {
            for (const uint8_t mask : {uint8_t{0x00}, uint8_t{0xFF}, uint8_t{0x55}}) {
                ByteBuffer data = good;
                data[position] = (mask == 0x55) ? static_cast<uint8_t>(data[position] ^ mask) : mask;

                BinaryDeserializer deserializer;
                if (deserializer.LoadFromBuffer(data)) {
                    ++accepted;
                    ReadRichDocument(deserializer);  // an accepted stream must be safe to read
                } else {
                    ++rejected;
                    EXPECT_TRUE(deserializer.HasErrors());
                }
            }
        }

        EXPECT_GT(rejected, 0u) << "corrupting the header must be detected";
        EXPECT_GT(accepted, 0u) << "changing a value byte keeps the stream structurally valid";
    }

    TEST(ReflectionTest, BinaryDeserializerCanBeReused) {
        const ByteBuffer good = MakeRichDocument();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(good));
        ASSERT_TRUE(deserializer.BeginObject("Doc"));  // leave the scope open on purpose

        // A failed load drops the previous stream
        EXPECT_FALSE(deserializer.LoadFromBuffer(good.data(), good.size() - 1));
        EXPECT_FALSE(deserializer.BeginObject("Doc"));

        // A later successful load starts at the root again
        ASSERT_TRUE(deserializer.LoadFromBuffer(good));
        EXPECT_TRUE(deserializer.BeginObject("Doc"));
    }

    TEST(ReflectionTest, BinaryFileRoundTrip) {
        TestData::GameState original;
        original.player.health = 33;
        original.player.name = PoolString::Intern("Persistent");
        original.inventory.gold = 12;
        original.level = 5;

        BinarySerializer serializer;
        if (serializer.BeginObject("Save")) {
            Ser::Save(serializer, original, "state");
            serializer.EndObject();
        }

        const std::filesystem::path path = TestTempPath("BagiEngineTests_BinaryFileRoundTrip");
        ASSERT_TRUE(serializer.SaveToFile(path));

        TestData::GameState loaded;
        {
            BinaryDeserializer deserializer;
            const bool loadedOk = deserializer.LoadFromFile(path);
            std::error_code removeError;
            std::filesystem::remove(path, removeError);

            ASSERT_TRUE(loadedOk) << DescribeErrors(deserializer).c_str();
            ASSERT_TRUE(deserializer.BeginObject("Save"));
            Ser::Load(deserializer, loaded, "state");
            deserializer.EndObject();
            EXPECT_FALSE(deserializer.HasErrors()) << DescribeErrors(deserializer).c_str();
        }

        ExpectPlayersEqual(loaded.player, original.player);
        EXPECT_EQ(loaded.inventory.gold, original.inventory.gold);
        EXPECT_EQ(loaded.level, original.level);
    }

    TEST(ReflectionTest, BinaryFileErrors) {
        const std::filesystem::path missing = TestTempPath("BagiEngineTests_DoesNotExist");
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);

        BinaryDeserializer deserializer;
        EXPECT_FALSE(deserializer.LoadFromFile(missing));
        EXPECT_TRUE(deserializer.HasErrors());

        const BinarySerializer serializer;
        EXPECT_FALSE(serializer.SaveToFile(std::filesystem::temp_directory_path() / "BagiEngineTests_NoSuchDirectory" / "file.bin"));

        // A file that is not a binary stream is rejected like a corrupt buffer
        const std::filesystem::path garbage = TestTempPath("BagiEngineTests_Garbage");
        {
            std::ofstream file(garbage, std::ios::binary | std::ios::trunc);
            file << "this is definitely not a BagiEngine binary stream";
        }
        BinaryDeserializer fromGarbage;
        const bool loaded = fromGarbage.LoadFromFile(garbage);
        std::filesystem::remove(garbage, ignored);
        EXPECT_FALSE(loaded);
        EXPECT_TRUE(fromGarbage.HasErrors());
    }

    TEST(ReflectionTest, MethodReflection) {
        int methodCount = 0;
        eastl::vector<StringView> methodNames;
        ForEachMethod<TestData::Player>([&](auto& method) {
            methodCount++;
            methodNames.push_back(method.name);
        });

        EXPECT_EQ(methodCount, 4) << "ForEachMethod should visit every reflected method";

        const auto hasName = [&methodNames](StringView expected) { return eastl::find(methodNames.begin(), methodNames.end(), expected) != methodNames.end(); };
        EXPECT_TRUE(hasName("TakeDamage"));
        EXPECT_TRUE(hasName("Heal"));
        EXPECT_TRUE(hasName("IsDead"));
        EXPECT_TRUE(hasName("GetHealthPercent"));

        TestData::Player player;
        player.health = 100;
        player.isAlive = true;

        // TakeDamage via reflection
        const int32_t remaining = InvokeMethod<int32_t>(player, "TakeDamage", 30);
        EXPECT_EQ(remaining, 70);
        EXPECT_EQ(player.health, 70) << "health after TakeDamage";

        // Heal via reflection
        InvokeMethod<void>(player, "Heal", 20);
        EXPECT_EQ(player.health, 90) << "health after Heal";

        // IsDead and GetHealthPercent are const methods, so they are checked by a direct call
        EXPECT_FALSE(player.IsDead());
        EXPECT_EQ(player.GetHealthPercent(), 90.0f);
    }

    TEST(ReflectionTest, DefaultChecker) {
        // Arithmetic types
        EXPECT_TRUE(DefaultChecker<int32_t>::IsDefault(0));
        EXPECT_FALSE(DefaultChecker<int32_t>::IsDefault(42));
        EXPECT_TRUE(DefaultChecker<float>::IsDefault(0.0f));
        EXPECT_TRUE(DefaultChecker<bool>::IsDefault(false));
        EXPECT_FALSE(DefaultChecker<bool>::IsDefault(true));

        // PoolString
        EXPECT_TRUE(DefaultChecker<PoolString>::IsDefault(PoolString{}));
        EXPECT_FALSE(DefaultChecker<PoolString>::IsDefault(PoolString::Intern("test")));

        // eastl::string
        EXPECT_TRUE(DefaultChecker<eastl::string>::IsDefault(eastl::string{}));
        EXPECT_FALSE(DefaultChecker<eastl::string>::IsDefault(eastl::string{"hello"}));

        // eastl::vector
        EXPECT_TRUE(DefaultChecker<eastl::vector<int32_t>>::IsDefault(eastl::vector<int32_t>{}));
        const eastl::vector<int32_t> nonEmptyVec{1, 2, 3};
        EXPECT_FALSE(DefaultChecker<eastl::vector<int32_t>>::IsDefault(nonEmptyVec));
    }

    TEST(ReflectionTest, SkipDefaults) {
        TestData::Player player;
        player.health = 0;
        player.speed = 0.0f;
        player.name = PoolString{};
        player.isAlive = false;

        const eastl::string fullXml = SavePlayer(player, false);
        const eastl::string skipXml = SavePlayer(player, true);

        EXPECT_LT(skipXml.size(), fullXml.size()) << "skip-defaults XML should be smaller:\n" << skipXml.c_str() << "\nvs\n" << fullXml.c_str();

        // Round-trip of the skip-defaults XML: the defaults must come back as the same values
        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;
        ASSERT_NO_FATAL_FAILURE(LoadPlayer(skipXml, loaded));
        ExpectPlayersEqual(loaded, player);

        // Non-default values must still be serialized
        TestData::Player hero;
        hero.health = 75;
        hero.speed = 10.5f;
        hero.name = PoolString::Intern("Hero");
        hero.isAlive = true;

        const eastl::string heroXml = SavePlayer(hero, true);

        TestData::Player heroLoaded;
        ASSERT_NO_FATAL_FAILURE(LoadPlayer(heroXml, heroLoaded));
        ExpectPlayersEqual(heroLoaded, hero);
    }

    TEST(ReflectionTest, MapSerialization) {
        eastl::map<eastl::string, int32_t> original;
        original["charlie"] = 3;
        original["alpha"] = 1;
        original["bravo"] = 2;

        const eastl::string xml = SaveToXml("MapTest", "scores", original);

        eastl::map<eastl::string, int32_t> loaded;
        ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "MapTest", "scores", loaded));

        ASSERT_EQ(loaded.size(), original.size());
        for (const auto& [k, v] : original) {
            auto it = loaded.find(k);
            ASSERT_TRUE(it != loaded.end()) << "map entry '" << k.c_str() << "' is missing";
            EXPECT_EQ(it->second, v) << "map entry '" << k.c_str() << "'";
        }
    }

    TEST(ReflectionTest, FixedVectorSerialization) {
        eastl::fixed_vector<int32_t, 8> original;
        original.push_back(10);
        original.push_back(20);
        original.push_back(30);

        const eastl::string xml = SaveToXml("FVTest", "values", original);

        eastl::fixed_vector<int32_t, 8> loaded;
        ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "FVTest", "values", loaded));

        ASSERT_EQ(loaded.size(), original.size());
        for (size_t i = 0; i < original.size(); ++i) {
            EXPECT_EQ(loaded[i], original[i]) << "fixed_vector[" << i << "]";
        }
    }

    TEST(ReflectionTest, ArraySerialization) {
        // eastl::array
        {
            SCOPED_TRACE("eastl::array");
            const eastl::array<float, 3> original = {1.0f, 2.5f, 3.75f};
            const eastl::string xml = SaveToXml("ArrTest", "values", original);

            eastl::array<float, 3> loaded = {};
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "ArrTest", "values", loaded));

            for (size_t i = 0; i < 3; ++i) {
                EXPECT_EQ(loaded[i], original[i]) << "index " << i;
            }
        }

        // std::array
        {
            SCOPED_TRACE("std::array");
            const std::array<float, 3> original = {4.0f, 5.5f, 6.25f};
            const eastl::string xml = SaveToXml("StdArrTest", "values", original);

            std::array<float, 3> loaded = {};
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "StdArrTest", "values", loaded));

            for (size_t i = 0; i < 3; ++i) {
                EXPECT_EQ(loaded[i], original[i]) << "index " << i;
            }
        }

        // Partial load: the XML has fewer elements than the array size
        {
            SCOPED_TRACE("partial load");
            const eastl::array<int32_t, 2> small = {7, 8};
            const eastl::string xml = SaveToXml("PartialArr", "values", small);

            eastl::array<int32_t, 4> loaded = {99, 99, 99, 99};
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "PartialArr", "values", loaded));

            EXPECT_EQ(loaded[0], 7);
            EXPECT_EQ(loaded[1], 8);
            EXPECT_EQ(loaded[2], 0) << "elements missing from the XML are value-initialized";
            EXPECT_EQ(loaded[3], 0) << "elements missing from the XML are value-initialized";
        }
    }

    TEST(ReflectionTest, PairSerialization) {
        // eastl::pair
        {
            SCOPED_TRACE("eastl::pair");
            const eastl::pair<int32_t, eastl::string> original = {42, eastl::string("hello")};
            const eastl::string xml = SaveToXml("PairTest", "entry", original);

            eastl::pair<int32_t, eastl::string> loaded = {0, eastl::string{}};
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "PairTest", "entry", loaded));

            EXPECT_EQ(loaded.first, original.first);
            EXPECT_EQ(loaded.second, original.second);
        }

        // std::pair
        {
            SCOPED_TRACE("std::pair");
            const std::pair<int32_t, eastl::string> original = {99, eastl::string("world")};
            const eastl::string xml = SaveToXml("StdPairTest", "entry", original);

            std::pair<int32_t, eastl::string> loaded = {0, eastl::string{}};
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "StdPairTest", "entry", loaded));

            EXPECT_EQ(loaded.first, original.first);
            EXPECT_EQ(loaded.second, original.second);
        }
    }

    TEST(ReflectionTest, OptionalSerialization) {
        // eastl::optional with a value
        {
            SCOPED_TRACE("eastl::optional with value");
            const Optional<int32_t> original = 77;
            const eastl::string xml = SaveToXml("OptTest", "value", original);

            Optional<int32_t> loaded;
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "OptTest", "value", loaded));

            ASSERT_TRUE(loaded.has_value());
            EXPECT_EQ(*loaded, 77);
        }

        // eastl::optional without a value (absent key -> nullopt)
        {
            SCOPED_TRACE("eastl::optional without value");
            const Optional<int32_t> original;
            const eastl::string xml = SaveToXml("OptEmptyTest", "value", original);

            Optional<int32_t> loaded = 55;  // pre-set to non-null to verify the reset
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "OptEmptyTest", "value", loaded));

            EXPECT_FALSE(loaded.has_value()) << "should be nullopt after loading an absent key";
        }

        // std::optional with a value
        {
            SCOPED_TRACE("std::optional with value");
            const Optional<int32_t> original = 123;
            const eastl::string xml = SaveToXml("StdOptTest", "value", original);

            Optional<int32_t> loaded;
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "StdOptTest", "value", loaded));

            ASSERT_TRUE(loaded.has_value());
            EXPECT_EQ(*loaded, 123);
        }
    }

    TEST(ReflectionTest, ErrorReporting) {
        // Nested structure: root > player > inventory. Read a field that doesn't exist inside inventory.
        constexpr StringView xml = R"(
            <root>
              <player>
                <inventory gold="50"/>
              </player>
            </root>
        )";

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml)) << "Failed to load error-reporting XML";

        // Read() reports errors; ReadAttribute() does not for missing attributes
        if (deserializer.BeginObject("player")) {
            if (deserializer.BeginObject("inventory")) {
                int32_t missing = 0;
                deserializer.Read("nonExistentField", missing);
                deserializer.EndObject();
            }
            deserializer.EndObject();
        }

        ASSERT_TRUE(deserializer.HasErrors()) << "Expected errors but none were reported";

        const auto& errors = deserializer.GetErrors();
        ASSERT_FALSE(errors.empty());
        const eastl::string pathStr = errors[0].path.CStr();

        // The path should contain "player" and "inventory"
        EXPECT_NE(pathStr.find("player"), eastl::string::npos) << "error path '" << pathStr.c_str() << "'";
        EXPECT_NE(pathStr.find("inventory"), eastl::string::npos) << "error path '" << pathStr.c_str() << "'";
    }

    TEST(ReflectionTest, UnorderedMapSortedKeys) {
        eastl::unordered_map<eastl::string, int32_t> original;
        original["zebra"] = 26;
        original["apple"] = 1;
        original["mango"] = 13;

        const eastl::string xml = SaveToXml("MapSortTest", "entries", original);

        // Keys must appear in alphabetical order in the XML text
        const size_t posApple = xml.find("apple");
        const size_t posMango = xml.find("mango");
        const size_t posZebra = xml.find("zebra");

        ASSERT_NE(posApple, eastl::string::npos) << xml.c_str();
        ASSERT_NE(posMango, eastl::string::npos) << xml.c_str();
        ASSERT_NE(posZebra, eastl::string::npos) << xml.c_str();
        EXPECT_TRUE(posApple < posMango && posMango < posZebra) << "keys not sorted: apple=" << posApple << ", mango=" << posMango << ", zebra=" << posZebra;

        // The round-trip must still work
        eastl::unordered_map<eastl::string, int32_t> loaded;
        ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "MapSortTest", "entries", loaded));

        ASSERT_EQ(loaded.size(), original.size());
        for (const auto& [k, v] : original) {
            auto it = loaded.find(k);
            ASSERT_TRUE(it != loaded.end()) << "entry '" << k.c_str() << "' is missing";
            EXPECT_EQ(it->second, v) << "entry '" << k.c_str() << "'";
        }
    }

}  // namespace BECore::Tests
