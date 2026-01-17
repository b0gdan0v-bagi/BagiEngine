#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/PoolString/PoolString.h>
#include <cstdint>

namespace BECore {

    // =========================================================================
    // Test data structures marked for reflection
    // =========================================================================

    namespace TestData {

        /**
         * @brief Example reflected class for testing
         */
        struct BE_REFLECT_CLASS Player {
            BE_REFLECT_FIELD int32_t health = 100;
            BE_REFLECT_FIELD float speed = 5.0f;
            BE_REFLECT_FIELD PoolString name;
            BE_REFLECT_FIELD bool isAlive = true;
        };

        /**
         * @brief Nested reflected class for testing
         */
        struct BE_REFLECT_CLASS Inventory {
            BE_REFLECT_FIELD int32_t gold = 0;
            BE_REFLECT_FIELD int32_t itemCount = 0;
        };

        /**
         * @brief Class with nested object for testing
         */
        struct BE_REFLECT_CLASS GameState {
            BE_REFLECT_FIELD Player player;
            BE_REFLECT_FIELD Inventory inventory;
            BE_REFLECT_FIELD int32_t level = 1;
        };

    }  // namespace TestData

    // =========================================================================
    // Reflection Test
    // =========================================================================

    /**
     * @brief Test for reflection and serialization system
     *
     * Tests:
     * - ReflectionTraits generation (requires generated code)
     * - XmlArchive serialization/deserialization
     * - BinaryArchive serialization/deserialization
     * - Nested object handling
     */
    class ReflectionTest : public ITest {
    public:
        ReflectionTest() = default;
        ~ReflectionTest() override = default;

        bool Run() override;
        const char* GetName() const override { return "ReflectionTest"; }

    private:
        /**
         * @brief Test basic field access via reflection
         */
        bool TestFieldAccess();

        /**
         * @brief Test XML serialization round-trip
         */
        bool TestXmlSerialization();

        /**
         * @brief Test binary serialization round-trip
         */
        bool TestBinarySerialization();
    };

}  // namespace BECore
