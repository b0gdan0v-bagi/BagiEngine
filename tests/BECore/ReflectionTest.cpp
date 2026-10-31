#include "ReflectionTest.h"

#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/optional.h>
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
        eastl::string SaveToXml(eastl::string_view objectName, eastl::string_view key, const T& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject(objectName)) {
                Ser::Save(serializer, value, key);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        /// Loads the @p key entry of <objectName> from @p xml into @p out. Use through ASSERT_NO_FATAL_FAILURE.
        template <typename T>
        void LoadFromXml(const eastl::string& xml, eastl::string_view objectName, eastl::string_view key, T& out) {
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
        ForEachField(player, [&](eastl::string_view, auto&) { fieldCount++; });

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

    // Placeholder for the BinarySerializer/BinaryDeserializer pair. It is DISABLED_ (not an empty passing test)
    // so that the missing coverage shows up in every test run until the serializers exist.
    TEST(ReflectionTest, DISABLED_BinarySerialization) {
        FAIL() << "BinarySerializer/BinaryDeserializer are not implemented yet";
    }

    TEST(ReflectionTest, MethodReflection) {
        int methodCount = 0;
        eastl::vector<eastl::string_view> methodNames;
        ForEachMethod<TestData::Player>([&](auto& method) {
            methodCount++;
            methodNames.push_back(method.name);
        });

        EXPECT_EQ(methodCount, 4) << "ForEachMethod should visit every reflected method";

        const auto hasName = [&methodNames](eastl::string_view expected) { return eastl::find(methodNames.begin(), methodNames.end(), expected) != methodNames.end(); };
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
            const eastl::optional<int32_t> original = 77;
            const eastl::string xml = SaveToXml("OptTest", "value", original);

            eastl::optional<int32_t> loaded;
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "OptTest", "value", loaded));

            ASSERT_TRUE(loaded.has_value());
            EXPECT_EQ(*loaded, 77);
        }

        // eastl::optional without a value (absent key -> nullopt)
        {
            SCOPED_TRACE("eastl::optional without value");
            const eastl::optional<int32_t> original;
            const eastl::string xml = SaveToXml("OptEmptyTest", "value", original);

            eastl::optional<int32_t> loaded = 55;  // pre-set to non-null to verify the reset
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "OptEmptyTest", "value", loaded));

            EXPECT_FALSE(loaded.has_value()) << "should be nullopt after loading an absent key";
        }

        // std::optional with a value
        {
            SCOPED_TRACE("std::optional with value");
            const std::optional<int32_t> original = 123;
            const eastl::string xml = SaveToXml("StdOptTest", "value", original);

            std::optional<int32_t> loaded;
            ASSERT_NO_FATAL_FAILURE(LoadFromXml(xml, "StdOptTest", "value", loaded));

            ASSERT_TRUE(loaded.has_value());
            EXPECT_EQ(*loaded, 123);
        }
    }

    TEST(ReflectionTest, ErrorReporting) {
        // Nested structure: root > player > inventory. Read a field that doesn't exist inside inventory.
        constexpr eastl::string_view xml = R"(
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
