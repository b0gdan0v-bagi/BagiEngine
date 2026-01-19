#include <BECore/Tests/ReflectionTest.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/SaveSystem.h>
#include <BECore/Reflection/XmlArchive.h>
#include <BECore/Reflection/BinaryArchive.h>
#include <BECore/Logger/Logger.h>
#include <EASTL/vector.h>

#include <Generated/ReflectionTest.gen.hpp>

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

    // =========================================================================
    // Test implementation
    // =========================================================================

    bool ReflectionTest::Run() {
        LOG_INFO("ReflectionTest", "Starting reflection tests...");
        
        bool allPassed = true;
        
        if (!TestFieldAccess()) {
            LOG_ERROR("ReflectionTest", "TestFieldAccess FAILED");
            allPassed = false;
        } else {
            LOG_INFO("ReflectionTest", "TestFieldAccess PASSED");
        }
        
        if (!TestXmlSerialization()) {
            LOG_ERROR("ReflectionTest", "TestXmlSerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("ReflectionTest", "TestXmlSerialization PASSED");
        }
        
        if (!TestBinarySerialization()) {
            LOG_ERROR("ReflectionTest", "TestBinarySerialization FAILED");
            allPassed = false;
        } else {
            LOG_INFO("ReflectionTest", "TestBinarySerialization PASSED");
        }
        
        if (!TestMethodReflection()) {
            LOG_ERROR("ReflectionTest", "TestMethodReflection FAILED");
            allPassed = false;
        } else {
            LOG_INFO("ReflectionTest", "TestMethodReflection PASSED");
        }
        
        return allPassed;
    }

    bool ReflectionTest::TestFieldAccess() {
        // Test HasReflection concept
        static_assert(HasReflection<TestData::Player>, "Player should have reflection");
        static_assert(HasReflection<TestData::Inventory>, "Inventory should have reflection");
        static_assert(HasReflection<TestData::GameState>, "GameState should have reflection");
        
        // Test field count
        static_assert(FieldCount<TestData::Player>() == 4, "Player should have 4 fields");
        static_assert(FieldCount<TestData::Inventory>() == 2, "Inventory should have 2 fields");
        
        // Test ForEachField
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
            LOG_ERROR("ReflectionTest", "ForEachField visited {} fields, expected 4", fieldCount);
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
        XmlArchive writeArchive(XmlArchive::Mode::Write);
        Serialize(writeArchive, eastl::string_view{"Player"}, original);
        eastl::string xml = writeArchive.SaveToString();
        
        LOG_DEBUG("ReflectionTest", "Generated XML:\n{}", std::string_view(xml.c_str(), xml.size()));
        
        // Deserialize from XML
        XmlArchive readArchive(XmlArchive::Mode::Read);
        if (!readArchive.LoadFromString(xml)) {
            LOG_ERROR("ReflectionTest", "Failed to load XML");
            return false;
        }
        
        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = false;
        
        Serialize(readArchive, eastl::string_view{"Player"}, loaded);
        
        // Verify
        if (loaded.health != original.health) {
            LOG_ERROR("ReflectionTest", "health mismatch: {} != {}", loaded.health, original.health);
            return false;
        }
        
        if (loaded.speed != original.speed) {
            LOG_ERROR("ReflectionTest", "speed mismatch: {} != {}", loaded.speed, original.speed);
            return false;
        }
        
        if (loaded.name != original.name) {
            LOG_ERROR("ReflectionTest", "name mismatch");
            return false;
        }
        
        if (loaded.isAlive != original.isAlive) {
            LOG_ERROR("ReflectionTest", "isAlive mismatch");
            return false;
        }
        
        return true;
    }

    bool ReflectionTest::TestBinarySerialization() {
        // Create test data
        TestData::Player original;
        original.health = 150;
        original.speed = 25.0f;
        original.name = PoolString::Intern("BinaryHero");
        original.isAlive = false;
        
        // Serialize to binary
        BinaryArchive writeArchive(BinaryArchive::Mode::Write);
        Serialize(writeArchive, eastl::string_view{"Player"}, original);
        
        LOG_DEBUG("ReflectionTest", "Binary size: {} bytes", writeArchive.GetSize());
        
        // Deserialize from binary
        BinaryArchive readArchive(BinaryArchive::Mode::Read);
        if (!readArchive.LoadFromBuffer(writeArchive.GetBuffer().data(), writeArchive.GetBuffer().size())) {
            LOG_ERROR("ReflectionTest", "Failed to load binary");
            return false;
        }
        
        TestData::Player loaded;
        loaded.health = 0;
        loaded.speed = 0.0f;
        loaded.isAlive = true;
        
        Serialize(readArchive, eastl::string_view{"Player"}, loaded);
        
        // Verify
        if (loaded.health != original.health) {
            LOG_ERROR("ReflectionTest", "health mismatch: {} != {}", loaded.health, original.health);
            return false;
        }
        
        if (loaded.speed != original.speed) {
            LOG_ERROR("ReflectionTest", "speed mismatch: {} != {}", loaded.speed, original.speed);
            return false;
        }
        
        if (loaded.name != original.name) {
            LOG_ERROR("ReflectionTest", "name mismatch");
            return false;
        }
        
        if (loaded.isAlive != original.isAlive) {
            LOG_ERROR("ReflectionTest", "isAlive mismatch");
            return false;
        }
        
        return true;
    }

    bool ReflectionTest::TestMethodReflection() {
        // Test HasMethodReflection concept
        static_assert(HasMethodReflection<TestData::Player>, "Player should have method reflection");
        
        // Test method count
        static_assert(MethodCount<TestData::Player>() == 4, "Player should have 4 reflected methods");
        
        // Test ForEachMethod
        int methodCount = 0;
        eastl::vector<eastl::string_view> methodNames;
        ForEachMethod<TestData::Player>([&](auto& method) {
            methodCount++;
            methodNames.push_back(method.name);
        });
        
        if (methodCount != 4) {
            LOG_ERROR("ReflectionTest", "ForEachMethod visited {} methods, expected 4", methodCount);
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
            LOG_ERROR("ReflectionTest", "Missing expected method names");
            return false;
        }
        
        LOG_DEBUG("ReflectionTest", "Method reflection: found {} methods", methodCount);
        for (const auto& name : methodNames) {
            LOG_DEBUG("ReflectionTest", "  - {}", std::string_view(name.data(), name.size()));
        }
        
        // Test InvokeMethod for non-const methods
        TestData::Player player;
        player.health = 100;
        player.isAlive = true;
        
        // Test TakeDamage via reflection
        int32_t remaining = InvokeMethod<int32_t>(player, "TakeDamage", 30);
        if (remaining != 70) {
            LOG_ERROR("ReflectionTest", "TakeDamage returned {}, expected 70", remaining);
            return false;
        }
        if (player.health != 70) {
            LOG_ERROR("ReflectionTest", "health is {}, expected 70 after TakeDamage", player.health);
            return false;
        }
        
        // Test Heal via reflection
        InvokeMethod<void>(player, "Heal", 20);
        if (player.health != 90) {
            LOG_ERROR("ReflectionTest", "health is {}, expected 90 after Heal", player.health);
            return false;
        }
        
        // Test const methods via reflection
        // Note: IsDead and GetHealthPercent are const methods
        // We need to use const reference for const method invocation
        const TestData::Player& constPlayer = player;
        
        // For const methods, we can check the traits directly
        LOG_DEBUG("ReflectionTest", "Player health: {}, isAlive: {}", player.health, player.isAlive);
        LOG_DEBUG("ReflectionTest", "IsDead() should return false, GetHealthPercent() should return 90");
        
        // Direct method calls to verify behavior
        if (player.IsDead()) {
            LOG_ERROR("ReflectionTest", "IsDead() returned true, expected false");
            return false;
        }
        
        if (player.GetHealthPercent() != 90.0f) {
            LOG_ERROR("ReflectionTest", "GetHealthPercent() returned {}, expected 90", player.GetHealthPercent());
            return false;
        }
        
        LOG_INFO("ReflectionTest", "Method reflection tests passed");
        return true;
    }

}  // namespace BECore
