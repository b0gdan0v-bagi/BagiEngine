#include <BECore/Tests/ReflectionTest.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/SaveSystem.h>
#include <BECore/Reflection/XmlArchive.h>
#include <BECore/Reflection/BinaryArchive.h>
#include <BECore/Logger/Logger.h>

#include <Generated/ReflectionTest.gen.hpp>

namespace BECore {

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

}  // namespace BECore
