#include <BECore/Tests/ReflectionTest.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <BECore/Reflection/DataAccessor.h>
#include <EASTL/map.h>
#include <EASTL/optional.h>

#include <Generated/ReflectionTest.gen.hpp>

// =========================================================================
// Compile-time tests (requires generated ReflectionTraits)
// =========================================================================

namespace BECore::Tests {

    constexpr void ReflectionTest::TestCompileTime() {
        // HasReflection concept tests
        static_assert(HasReflection<TestData::Player>, "Player should have reflection");
        static_assert(HasReflection<TestData::Inventory>, "Inventory should have reflection");
        static_assert(HasReflection<TestData::GameState>, "GameState should have reflection");

        // Field count tests
        static_assert(FieldCount<TestData::Player>() == 4, "Player should have 4 fields");
        static_assert(FieldCount<TestData::Inventory>() == 2, "Inventory should have 2 fields");
        static_assert(FieldCount<TestData::GameState>() == 3, "GameState should have 3 fields");

        // HasField tests
        static_assert(HasField<TestData::Player>("health"), "Player should have 'health' field");
        static_assert(HasField<TestData::Player>("speed"), "Player should have 'speed' field");
        static_assert(HasField<TestData::Player>("name"), "Player should have 'name' field");
        static_assert(HasField<TestData::Player>("isAlive"), "Player should have 'isAlive' field");
        static_assert(!HasField<TestData::Player>("mana"), "Player should not have 'mana' field");

        // GetFieldInfo tests
        static_assert(GetFieldInfo<TestData::Player, 0>().name == "health");
        static_assert(GetFieldInfo<TestData::Player, 1>().name == "speed");
        static_assert(GetFieldInfo<TestData::Player, 2>().name == "name");
        static_assert(GetFieldInfo<TestData::Player, 3>().name == "isAlive");

        // Method reflection tests
        static_assert(HasMethodReflection<TestData::Player>, "Player should have method reflection");
        static_assert(MethodCount<TestData::Player>() == 4, "Player should have 4 reflected methods");

        // HasMethod tests
        static_assert(HasMethod<TestData::Player>("TakeDamage"), "Player should have 'TakeDamage' method");
        static_assert(HasMethod<TestData::Player>("Heal"), "Player should have 'Heal' method");
        static_assert(HasMethod<TestData::Player>("IsDead"), "Player should have 'IsDead' method");
        static_assert(HasMethod<TestData::Player>("GetHealthPercent"), "Player should have 'GetHealthPercent' method");
        static_assert(!HasMethod<TestData::Player>("Fly"), "Player should not have 'Fly' method");

        // GetMethodInfo tests
        static_assert(GetMethodInfo<TestData::Player, 0>().name == "TakeDamage");
        static_assert(GetMethodInfo<TestData::Player, 1>().name == "Heal");
    }

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
// Test implementation
// =========================================================================

namespace BECore::Tests {

    bool ReflectionTest::Run() {
        LOG_INFO("[ReflectionTest] Starting reflection tests...");
        
        // Compile-time tests (static_assert)
        TestCompileTime();
        LOG_INFO("[ReflectionTest] Compile-time tests PASSED");
        
        bool allPassed = true;
        
        if (!TestFieldAccess()) {
            LOG_ERROR("[ReflectionTest] TestFieldAccess FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestFieldAccess PASSED");
        }
        
        if (!TestXmlSerialization()) {
            LOG_ERROR("[ReflectionTest] TestXmlSerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestXmlSerialization PASSED");
        }
        
        // TODO: Re-enable once BinarySerializer/BinaryDeserializer are implemented
        // BinaryArchive uses deprecated IArchive interface
        /*
        if (!TestBinarySerialization()) {
            LOG_ERROR("[ReflectionTest] TestBinarySerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestBinarySerialization PASSED");
        }
        */
        LOG_INFO("[ReflectionTest] TestBinarySerialization SKIPPED (BinarySerializer not implemented)");
        
        if (!TestMethodReflection()) {
            LOG_ERROR("[ReflectionTest] TestMethodReflection FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestMethodReflection PASSED");
        }

        if (!TestDefaultChecker()) {
            LOG_ERROR("[ReflectionTest] TestDefaultChecker FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestDefaultChecker PASSED");
        }

        if (!TestSkipDefaults()) {
            LOG_ERROR("[ReflectionTest] TestSkipDefaults FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestSkipDefaults PASSED");
        }

        if (!TestMapSerialization()) {
            LOG_ERROR("[ReflectionTest] TestMapSerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestMapSerialization PASSED");
        }

        if (!TestFixedVectorSerialization()) {
            LOG_ERROR("[ReflectionTest] TestFixedVectorSerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestFixedVectorSerialization PASSED");
        }

        if (!TestArraySerialization()) {
            LOG_ERROR("[ReflectionTest] TestArraySerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestArraySerialization PASSED");
        }

        if (!TestPairSerialization()) {
            LOG_ERROR("[ReflectionTest] TestPairSerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestPairSerialization PASSED");
        }

        if (!TestOptionalSerialization()) {
            LOG_ERROR("[ReflectionTest] TestOptionalSerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestOptionalSerialization PASSED");
        }

        if (!TestErrorReporting()) {
            LOG_ERROR("[ReflectionTest] TestErrorReporting FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestErrorReporting PASSED");
        }

        if (!TestUnorderedMapSortedKeys()) {
            LOG_ERROR("[ReflectionTest] TestUnorderedMapSortedKeys FAILED");
            allPassed = false;
        } else {
            LOG_INFO("[ReflectionTest] TestUnorderedMapSortedKeys PASSED");
        }

        return allPassed;
    }

    bool ReflectionTest::TestFieldAccess() {
        // Compile-time tests are in TestCompileTime()
        
        // Test ForEachField at runtime
        TestData::Player player;
        player.health = 42;
        player.speed = 3.14f;
        player.name = PoolString::Intern("TestPlayer");
        player.isAlive = false;
        
        int fieldCount = 0;
        ForEachField(player, [&](eastl::string_view name, auto& value) {
            fieldCount++;
            if (name == "health") {
                if constexpr (std::is_same_v<std::remove_reference_t<decltype(value)>, int32_t>) {
                    if (value != 42) return;
                }
            }
        });
        
        if (fieldCount != 4) {
            LOG_ERROR("[ReflectionTest] ForEachField visited {} fields, expected 4"_format(fieldCount));
            return false;
        }
        
        return true;
    }

    bool ReflectionTest::TestXmlSerialization() {
        // Create test data
        TestData::Player original;
        original.health = 75;
        original.speed = 10.5f;
        original.name = PoolString::Intern("Hero");
        original.isAlive = true;
        
        // Serialize to XML
        XmlSerializer serializer;
        if (serializer.BeginObject("Player")) {
            original.Serialize(serializer);
            serializer.EndObject();
        }
        eastl::string xml = serializer.SaveToString();
        
        LOG_DEBUG("[ReflectionTest] Generated XML:\n{}"_format(xml));
        
        // Deserialize from XML
        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(xml)) {
            LOG_ERROR("[ReflectionTest] Failed to load XML");
            return false;
        }
        
        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;
        
        if (deserializer.BeginObject("Player")) {
            loaded.Deserialize(deserializer);
            deserializer.EndObject();
        }
        
        // Verify
        if (loaded.health != original.health) {
            LOG_ERROR("[ReflectionTest] health mismatch: {} != {}"_format(loaded.health, original.health));
            return false;
        }
        
        if (loaded.speed != original.speed) {
            LOG_ERROR("[ReflectionTest] speed mismatch: {} != {}"_format(loaded.speed, original.speed));
            return false;
        }
        
        if (loaded.name != original.name) {
            LOG_ERROR("[ReflectionTest] name mismatch");
            return false;
        }
        
        if (loaded.isAlive != original.isAlive) {
            LOG_ERROR("[ReflectionTest] isAlive mismatch");
            return false;
        }
        
        return true;
    }

    bool ReflectionTest::TestBinarySerialization() {
        // TODO: Implement BinarySerializer and BinaryDeserializer
        // The old BinaryArchive uses deprecated IArchive interface
        LOG_INFO("[ReflectionTest] Binary serialization test skipped (not implemented)");
        return true;  // Skip test for now
    }

    bool ReflectionTest::TestMethodReflection() {
        // Compile-time tests are in TestCompileTime()
        
        // Test ForEachMethod at runtime
        int methodCount = 0;
        eastl::vector<eastl::string_view> methodNames;
        ForEachMethod<TestData::Player>([&](auto& method) {
            methodCount++;
            methodNames.push_back(method.name);
        });
        
        if (methodCount != 4) {
            LOG_ERROR("[ReflectionTest] ForEachMethod visited {} methods, expected 4"_format(methodCount));
            return false;
        }
        
        // Verify method names are present
        bool hasTakeDamage = false, hasHeal = false, hasIsDead = false, hasGetHealthPercent = false;
        for (const auto& name : methodNames) {
            if (name == "TakeDamage") hasTakeDamage = true;
            if (name == "Heal") hasHeal = true;
            if (name == "IsDead") hasIsDead = true;
            if (name == "GetHealthPercent") hasGetHealthPercent = true;
        }
        
        if (!hasTakeDamage || !hasHeal || !hasIsDead || !hasGetHealthPercent) {
            LOG_ERROR("[ReflectionTest] Missing expected method names");
            return false;
        }
        
        LOG_DEBUG("[ReflectionTest] Method reflection: found {} methods"_format(methodCount));
        for (const auto& name : methodNames) {
            LOG_DEBUG("[ReflectionTest]   - {}"_format(name));
        }
        
        // Test InvokeMethod for non-const methods
        TestData::Player player;
        player.health = 100;
        player.isAlive = true;
        
        // Test TakeDamage via reflection
        int32_t remaining = InvokeMethod<int32_t>(player, "TakeDamage", 30);
        if (remaining != 70) {
            LOG_ERROR("[ReflectionTest] TakeDamage returned {}, expected 70"_format(remaining));
            return false;
        }
        if (player.health != 70) {
            LOG_ERROR("[ReflectionTest] health is {}, expected 70 after TakeDamage"_format(player.health));
            return false;
        }
        
        // Test Heal via reflection
        InvokeMethod<void>(player, "Heal", 20);
        if (player.health != 90) {
            LOG_ERROR("[ReflectionTest] health is {}, expected 90 after Heal"_format(player.health));
            return false;
        }
        
        // Test const methods via reflection
        // Note: IsDead and GetHealthPercent are const methods
        // We need to use const reference for const method invocation
        const TestData::Player& constPlayer = player;
        
        // For const methods, we can check the traits directly
        LOG_DEBUG("[ReflectionTest] Player health: {}, isAlive: {}"_format(player.health, player.isAlive));
        LOG_DEBUG("[ReflectionTest] IsDead() should return false, GetHealthPercent() should return 90");
        
        // Direct method calls to verify behavior
        if (player.IsDead()) {
            LOG_ERROR("[ReflectionTest] IsDead() returned true, expected false");
            return false;
        }
        
        if (player.GetHealthPercent() != 90.0f) {
            LOG_ERROR("[ReflectionTest] GetHealthPercent() returned {}, expected 90"_format(player.GetHealthPercent()));
            return false;
        }
        
        LOG_INFO("[ReflectionTest] Method reflection tests passed");
        return true;
    }

    bool ReflectionTest::TestDefaultChecker() {
        // Arithmetic types
        if (!DefaultChecker<int32_t>::IsDefault(0)) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<int32_t>::IsDefault(0) should be true");
            return false;
        }
        if (DefaultChecker<int32_t>::IsDefault(42)) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<int32_t>::IsDefault(42) should be false");
            return false;
        }
        if (!DefaultChecker<float>::IsDefault(0.0f)) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<float>::IsDefault(0.0f) should be true");
            return false;
        }
        if (!DefaultChecker<bool>::IsDefault(false)) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<bool>::IsDefault(false) should be true");
            return false;
        }
        if (DefaultChecker<bool>::IsDefault(true)) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<bool>::IsDefault(true) should be false");
            return false;
        }

        // PoolString
        if (!DefaultChecker<PoolString>::IsDefault(PoolString{})) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<PoolString>::IsDefault(empty) should be true");
            return false;
        }
        if (DefaultChecker<PoolString>::IsDefault(PoolString::Intern("test"))) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<PoolString>::IsDefault(\"test\") should be false");
            return false;
        }

        // eastl::string
        if (!DefaultChecker<eastl::string>::IsDefault(eastl::string{})) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<eastl::string>::IsDefault(empty) should be true");
            return false;
        }
        if (DefaultChecker<eastl::string>::IsDefault(eastl::string{"hello"})) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<eastl::string>::IsDefault(\"hello\") should be false");
            return false;
        }

        // eastl::vector
        if (!DefaultChecker<eastl::vector<int32_t>>::IsDefault(eastl::vector<int32_t>{})) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<vector<int>>::IsDefault(empty) should be true");
            return false;
        }
        eastl::vector<int32_t> nonEmptyVec{1, 2, 3};
        if (DefaultChecker<eastl::vector<int32_t>>::IsDefault(nonEmptyVec)) {
            LOG_ERROR("[ReflectionTest] DefaultChecker<vector<int>>::IsDefault({1,2,3}) should be false");
            return false;
        }

        LOG_INFO("[ReflectionTest] DefaultChecker tests passed");
        return true;
    }

    bool ReflectionTest::TestSkipDefaults() {
        TestData::Player player;
        player.health = 0;
        player.speed = 0.0f;
        player.name = PoolString{};
        player.isAlive = false;

        // Serialize WITHOUT skip-defaults
        XmlSerializer fullSerializer;
        if (fullSerializer.BeginObject("Player")) {
            player.Serialize(fullSerializer);
            fullSerializer.EndObject();
        }
        eastl::string fullXml = fullSerializer.SaveToString();

        // Serialize WITH skip-defaults
        XmlSerializer skipSerializer;
        skipSerializer.SetSkipDefaults(true);
        if (skipSerializer.BeginObject("Player")) {
            player.Serialize(skipSerializer);
            skipSerializer.EndObject();
        }
        eastl::string skipXml = skipSerializer.SaveToString();

        LOG_DEBUG("[ReflectionTest] Full XML ({} chars):\n{}"_format(fullXml.size(), fullXml));
        LOG_DEBUG("[ReflectionTest] Skip-defaults XML ({} chars):\n{}"_format(skipXml.size(), skipXml));

        if (skipXml.size() >= fullXml.size()) {
            LOG_ERROR("[ReflectionTest] skip-defaults XML should be smaller: {} >= {}"_format(
                skipXml.size(), fullXml.size()));
            return false;
        }

        // Verify round-trip: deserialize skip-defaults XML and check values
        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(skipXml)) {
            LOG_ERROR("[ReflectionTest] Failed to load skip-defaults XML");
            return false;
        }

        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;

        if (deserializer.BeginObject("Player")) {
            loaded.Deserialize(deserializer);
            deserializer.EndObject();
        }

        if (loaded.health != player.health || loaded.speed != player.speed
            || loaded.name != player.name || loaded.isAlive != player.isAlive) {
            LOG_ERROR("[ReflectionTest] Round-trip with skip-defaults produced different values");
            return false;
        }

        // Verify that non-default values still serialize correctly
        TestData::Player hero;
        hero.health = 75;
        hero.speed = 10.5f;
        hero.name = PoolString::Intern("Hero");
        hero.isAlive = true;

        XmlSerializer heroSerializer;
        heroSerializer.SetSkipDefaults(true);
        if (heroSerializer.BeginObject("Player")) {
            hero.Serialize(heroSerializer);
            heroSerializer.EndObject();
        }
        eastl::string heroXml = heroSerializer.SaveToString();

        XmlDeserializer heroDeserializer;
        if (!heroDeserializer.LoadFromString(heroXml)) {
            LOG_ERROR("[ReflectionTest] Failed to load hero XML");
            return false;
        }

        TestData::Player heroLoaded;
        if (heroDeserializer.BeginObject("Player")) {
            heroLoaded.Deserialize(heroDeserializer);
            heroDeserializer.EndObject();
        }

        if (heroLoaded.health != hero.health || heroLoaded.speed != hero.speed
            || heroLoaded.name != hero.name || heroLoaded.isAlive != hero.isAlive) {
            LOG_ERROR("[ReflectionTest] Round-trip of non-default values with skip-defaults failed");
            return false;
        }

        LOG_INFO("[ReflectionTest] Skip-defaults tests passed");
        return true;
    }

    bool ReflectionTest::TestMapSerialization() {
        eastl::map<eastl::string, int32_t> original;
        original["charlie"] = 3;
        original["alpha"] = 1;
        original["bravo"] = 2;

        XmlSerializer serializer;
        if (serializer.BeginObject("MapTest")) {
            Ser::Save(serializer, original, "scores");
            serializer.EndObject();
        }
        eastl::string xml = serializer.SaveToString();
        LOG_DEBUG("[ReflectionTest] Map XML:\n{}"_format(xml));

        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(xml)) {
            LOG_ERROR("[ReflectionTest] Failed to load map XML");
            return false;
        }

        eastl::map<eastl::string, int32_t> loaded;
        if (deserializer.BeginObject("MapTest")) {
            Ser::Load(deserializer, loaded, "scores");
            deserializer.EndObject();
        }

        if (loaded.size() != original.size()) {
            LOG_ERROR("[ReflectionTest] map size mismatch: {} != {}"_format(loaded.size(), original.size()));
            return false;
        }
        for (const auto& [k, v] : original) {
            auto it = loaded.find(k);
            if (it == loaded.end() || it->second != v) {
                LOG_ERROR("[ReflectionTest] map entry '{}' mismatch"_format(k));
                return false;
            }
        }
        return true;
    }

    bool ReflectionTest::TestFixedVectorSerialization() {
        eastl::fixed_vector<int32_t, 8> original;
        original.push_back(10);
        original.push_back(20);
        original.push_back(30);

        XmlSerializer serializer;
        if (serializer.BeginObject("FVTest")) {
            Ser::Save(serializer, original, "values");
            serializer.EndObject();
        }
        eastl::string xml = serializer.SaveToString();

        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(xml)) {
            LOG_ERROR("[ReflectionTest] Failed to load fixed_vector XML");
            return false;
        }

        eastl::fixed_vector<int32_t, 8> loaded;
        if (deserializer.BeginObject("FVTest")) {
            Ser::Load(deserializer, loaded, "values");
            deserializer.EndObject();
        }

        if (loaded.size() != original.size()) {
            LOG_ERROR("[ReflectionTest] fixed_vector size mismatch: {} != {}"_format(loaded.size(), original.size()));
            return false;
        }
        for (size_t i = 0; i < original.size(); ++i) {
            if (loaded[i] != original[i]) {
                LOG_ERROR("[ReflectionTest] fixed_vector[{}] mismatch: {} != {}"_format(i, loaded[i], original[i]));
                return false;
            }
        }
        return true;
    }

    bool ReflectionTest::TestArraySerialization() {
        // eastl::array
        {
            eastl::array<float, 3> original = {1.0f, 2.5f, 3.75f};

            XmlSerializer serializer;
            if (serializer.BeginObject("ArrTest")) {
                Ser::Save(serializer, original, "values");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load eastl::array XML");
                return false;
            }

            eastl::array<float, 3> loaded = {};
            if (deserializer.BeginObject("ArrTest")) {
                Ser::Load(deserializer, loaded, "values");
                deserializer.EndObject();
            }

            for (size_t i = 0; i < 3; ++i) {
                if (loaded[i] != original[i]) {
                    LOG_ERROR("[ReflectionTest] eastl::array[{}] mismatch: {} != {}"_format(i, loaded[i], original[i]));
                    return false;
                }
            }
        }

        // std::array
        {
            std::array<float, 3> original = {4.0f, 5.5f, 6.25f};

            XmlSerializer serializer;
            if (serializer.BeginObject("StdArrTest")) {
                Ser::Save(serializer, original, "values");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load std::array XML");
                return false;
            }

            std::array<float, 3> loaded = {};
            if (deserializer.BeginObject("StdArrTest")) {
                Ser::Load(deserializer, loaded, "values");
                deserializer.EndObject();
            }

            for (size_t i = 0; i < 3; ++i) {
                if (loaded[i] != original[i]) {
                    LOG_ERROR("[ReflectionTest] std::array[{}] mismatch: {} != {}"_format(i, loaded[i], original[i]));
                    return false;
                }
            }
        }

        // Partial load: XML has fewer elements than array size
        {
            eastl::array<int32_t, 4> original = {7, 8, 0, 0};

            XmlSerializer serializer;
            if (serializer.BeginObject("PartialArr")) {
                eastl::array<int32_t, 2> small = {7, 8};
                Ser::Save(serializer, small, "values");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load partial array XML");
                return false;
            }

            eastl::array<int32_t, 4> loaded = {99, 99, 99, 99};
            if (deserializer.BeginObject("PartialArr")) {
                Ser::Load(deserializer, loaded, "values");
                deserializer.EndObject();
            }

            if (loaded[0] != 7 || loaded[1] != 8 || loaded[2] != 0 || loaded[3] != 0) {
                LOG_ERROR("[ReflectionTest] partial array load failed: [{}, {}, {}, {}]"_format(
                    loaded[0], loaded[1], loaded[2], loaded[3]));
                return false;
            }
        }

        return true;
    }

    bool ReflectionTest::TestPairSerialization() {
        // eastl::pair
        {
            eastl::pair<int32_t, eastl::string> original = {42, eastl::string("hello")};

            XmlSerializer serializer;
            if (serializer.BeginObject("PairTest")) {
                Ser::Save(serializer, original, "entry");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load eastl::pair XML");
                return false;
            }

            eastl::pair<int32_t, eastl::string> loaded = {0, eastl::string{}};
            if (deserializer.BeginObject("PairTest")) {
                Ser::Load(deserializer, loaded, "entry");
                deserializer.EndObject();
            }

            if (loaded.first != original.first || loaded.second != original.second) {
                LOG_ERROR("[ReflectionTest] eastl::pair mismatch: ({}, {}) != ({}, {})"_format(
                    loaded.first, loaded.second, original.first, original.second));
                return false;
            }
        }

        // std::pair
        {
            std::pair<int32_t, eastl::string> original = {99, eastl::string("world")};

            XmlSerializer serializer;
            if (serializer.BeginObject("StdPairTest")) {
                Ser::Save(serializer, original, "entry");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load std::pair XML");
                return false;
            }

            std::pair<int32_t, eastl::string> loaded = {0, eastl::string{}};
            if (deserializer.BeginObject("StdPairTest")) {
                Ser::Load(deserializer, loaded, "entry");
                deserializer.EndObject();
            }

            if (loaded.first != original.first || loaded.second != original.second) {
                LOG_ERROR("[ReflectionTest] std::pair mismatch");
                return false;
            }
        }

        return true;
    }

    bool ReflectionTest::TestOptionalSerialization() {
        // eastl::optional with value
        {
            eastl::optional<int32_t> original = 77;

            XmlSerializer serializer;
            if (serializer.BeginObject("OptTest")) {
                Ser::Save(serializer, original, "value");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load eastl::optional XML");
                return false;
            }

            eastl::optional<int32_t> loaded;
            if (deserializer.BeginObject("OptTest")) {
                Ser::Load(deserializer, loaded, "value");
                deserializer.EndObject();
            }

            if (!loaded.has_value() || *loaded != 77) {
                LOG_ERROR("[ReflectionTest] eastl::optional with value mismatch");
                return false;
            }
        }

        // eastl::optional without value (absent key -> nullopt)
        {
            eastl::optional<int32_t> original;  // no value

            XmlSerializer serializer;
            if (serializer.BeginObject("OptEmptyTest")) {
                Ser::Save(serializer, original, "value");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load empty optional XML");
                return false;
            }

            eastl::optional<int32_t> loaded = 55;  // pre-set to non-null to verify reset
            if (deserializer.BeginObject("OptEmptyTest")) {
                Ser::Load(deserializer, loaded, "value");
                deserializer.EndObject();
            }

            if (loaded.has_value()) {
                LOG_ERROR("[ReflectionTest] eastl::optional should be nullopt after loading absent key");
                return false;
            }
        }

        // std::optional with value
        {
            std::optional<int32_t> original = 123;

            XmlSerializer serializer;
            if (serializer.BeginObject("StdOptTest")) {
                Ser::Save(serializer, original, "value");
                serializer.EndObject();
            }
            eastl::string xml = serializer.SaveToString();

            XmlDeserializer deserializer;
            if (!deserializer.LoadFromString(xml)) {
                LOG_ERROR("[ReflectionTest] Failed to load std::optional XML");
                return false;
            }

            std::optional<int32_t> loaded;
            if (deserializer.BeginObject("StdOptTest")) {
                Ser::Load(deserializer, loaded, "value");
                deserializer.EndObject();
            }

            if (!loaded.has_value() || *loaded != 123) {
                LOG_ERROR("[ReflectionTest] std::optional with value mismatch");
                return false;
            }
        }

        return true;
    }

    bool ReflectionTest::TestErrorReporting() {
        // Build XML that has a nested structure: Root > player > inventory > gold
        // Then try to read a field that doesn't exist inside inventory
        constexpr eastl::string_view xml = R"(
            <root>
              <player>
                <inventory gold="50"/>
              </player>
            </root>
        )";

        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(xml)) {
            LOG_ERROR("[ReflectionTest] Failed to load error-reporting XML");
            return false;
        }

        // Navigate into player.inventory and read a non-existent child element
        // (Read() reports errors; ReadAttribute() does not for missing attrs)
        if (deserializer.BeginObject("player")) {
            if (deserializer.BeginObject("inventory")) {
                int32_t missing = 0;
                deserializer.Read("nonExistentField", missing);
                deserializer.EndObject();
            }
            deserializer.EndObject();
        }

        if (!deserializer.HasErrors()) {
            LOG_ERROR("[ReflectionTest] Expected errors but none were reported");
            return false;
        }

        const auto& errors = deserializer.GetErrors();
        const eastl::string pathStr = errors[0].path.ToStringView().data();
        LOG_DEBUG("[ReflectionTest] Error path: '{}', message: '{}'"_format(
            pathStr, errors[0].errorMessage.ToStringView()));

        // The path should contain "player" and "inventory"
        if (pathStr.find("player") == eastl::string::npos
            || pathStr.find("inventory") == eastl::string::npos) {
            LOG_ERROR("[ReflectionTest] Error path '{}' missing expected segments"_format(pathStr));
            return false;
        }

        return true;
    }

    bool ReflectionTest::TestUnorderedMapSortedKeys() {
        eastl::unordered_map<eastl::string, int32_t> original;
        original["zebra"] = 26;
        original["apple"] = 1;
        original["mango"] = 13;

        XmlSerializer serializer;
        if (serializer.BeginObject("MapSortTest")) {
            Ser::Save(serializer, original, "entries");
            serializer.EndObject();
        }
        eastl::string xml = serializer.SaveToString();
        LOG_DEBUG("[ReflectionTest] Sorted unordered_map XML:\n{}"_format(xml));

        // Verify keys appear in alphabetical order in the XML string
        size_t posApple = xml.find("apple");
        size_t posMango = xml.find("mango");
        size_t posZebra = xml.find("zebra");

        if (posApple == eastl::string::npos || posMango == eastl::string::npos
            || posZebra == eastl::string::npos) {
            LOG_ERROR("[ReflectionTest] Not all keys found in XML");
            return false;
        }

        if (!(posApple < posMango && posMango < posZebra)) {
            LOG_ERROR("[ReflectionTest] Keys not in sorted order in XML: apple={}, mango={}, zebra={}"_format(
                posApple, posMango, posZebra));
            return false;
        }

        // Verify round-trip still works
        XmlDeserializer deserializer;
        if (!deserializer.LoadFromString(xml)) {
            LOG_ERROR("[ReflectionTest] Failed to load sorted unordered_map XML");
            return false;
        }

        eastl::unordered_map<eastl::string, int32_t> loaded;
        if (deserializer.BeginObject("MapSortTest")) {
            Ser::Load(deserializer, loaded, "entries");
            deserializer.EndObject();
        }

        if (loaded.size() != original.size()) {
            LOG_ERROR("[ReflectionTest] unordered_map round-trip size mismatch");
            return false;
        }
        for (const auto& [k, v] : original) {
            auto it = loaded.find(k);
            if (it == loaded.end() || it->second != v) {
                LOG_ERROR("[ReflectionTest] unordered_map entry '{}' mismatch"_format(k));
                return false;
            }
        }

        return true;
    }

}  // namespace BECore::Tests
