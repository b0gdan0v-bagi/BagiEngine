#pragma once

#include <BECore/Tests/ITest.h>
#include <EASTL/vector.h>

namespace BECore {

    /**
     * @brief Enum defining available test types
     */
    CORE_ENUM(TestType, uint8_t, PoolStringTest, FormatTest, EnumUtilsTest, ReflectionTest)

    /**
     * @brief Manager for running engine tests based on XML configuration
     *
     * Reads TestsConfig.xml and runs tests specified in the config.
     * Tests can be enabled/disabled through configuration.
     *
     * @note Access via CoreManager::GetTestManager()
     *
     * @example
     * CoreManager::GetTestManager().RunAllTests();
     */
    class TestManager {
    public:
        TestManager() = default;
        ~TestManager() = default;

        /**
         * @brief Load and run tests from configuration
         *
         * Reads config/TestsConfig.xml and runs all enabled tests.
         */
        void RunAllTests();

    private:
        /**
         * @brief Create test instance by type
         * @param type Test type from enum
         * @return Pointer to test instance or nullptr if unknown type
         */
        static IntrusivePtr<ITest> CreateTestByType(TestType type);

        eastl::vector<IntrusivePtr<ITest>> _tests;
    };

}  // namespace BECore
