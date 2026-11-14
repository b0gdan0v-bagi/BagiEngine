#pragma once

#include <BECore/Tests/ITest.h>

namespace BECore::Tests {

    /**
     * @brief Runtime smoke test: checks the core services right after OnApplicationPreInit.
     *
     * Runs inside the live engine at startup (enabled in config/TestsConfig.xml), so it can rely on
     * CoreManager services. It is also the reference example of a runtime ITest. Unit and module tests
     * are GoogleTest tests in tests/ (target BagiEngineTests).
     */
    class EngineSmokeTest : public ITest {
        BE_CLASS(EngineSmokeTest)
    public:
        EngineSmokeTest() = default;
        ~EngineSmokeTest() override = default;

        bool Run() override;
        StringView GetName() override {
            return GetStaticTypeName();
        }
    };

    // Compile-time validation of test class
    static_assert(ValidTest<EngineSmokeTest>);

}  // namespace BECore::Tests
