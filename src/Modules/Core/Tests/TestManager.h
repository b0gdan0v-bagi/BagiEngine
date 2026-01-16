#pragma once

#include <Core/Tests/ITest.h>
#include <Core/Utils/Singleton.h>
#include <EASTL/vector.h>

namespace Core {

    /**
     * @brief Enum defining available test types
     */
    CORE_ENUM(TestType, uint8_t, PoolStringTest, FormatTest)

    /**
     * @brief Manager for running engine tests based on XML configuration
     *
     * Reads TestsConfig.xml and runs tests specified in the config.
     * Tests can be enabled/disabled through configuration.
     *
     * @example
     * TestManager::GetInstance().RunAllTests();
     */
    class TestManager : public Singleton<TestManager> {
    public:
        TestManager() = default;
        ~TestManager() override = default;

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

}  // namespace Core
