#include "TestManager.h"

#include <BECore/Tests/PoolStringTest.h>
#include <BECore/Tests/FormatTest.h>
#include <BECore/Tests/EnumUtilsTest.h>
#include <BECore/Tests/ReflectionTest.h>
#include <BECore/Config/XmlConfig.h>

namespace BECore {

    void TestManager::RunAllTests() {
        XmlConfig config = XmlConfig::Create();
        constexpr eastl::string_view configPath = "config/TestsConfig.xml";

        if (!config.LoadFromVirtualPath(configPath)) {
            LOG_ERROR("[TestManager] Config not found: {}", configPath);
            return;
        }

        const auto rootNode = config.GetRoot();
        if (!rootNode) {
            return;
        }

        const auto testsNode = rootNode.GetChild("tests");
        if (!testsNode) {
            return;
        }

        int passed = 0;
        int failed = 0;

        for (const auto testNode : testsNode.Children()) {
            if (testNode.Name() != "test") {
                continue;
            }

            // Check if test is enabled (default: true)
            auto enabled = testNode.ParseAttribute<bool>("enabled");
            if (enabled.has_value() && !enabled.value()) {
                continue;
            }

            auto testType = testNode.ParseAttribute<TestType>("type");
            if (!testType) {
                continue;
            }

            auto testPtr = CreateTestByType(*testType);
            if (!testPtr) {
                continue;
            }

            LOG_INFO("[TestManager] Running: {}", testPtr->GetName());

            if (testPtr->Run()) {
                ++passed;
                LOG_INFO("[TestManager] PASSED: {}", testPtr->GetName());
            } else {
                ++failed;
                LOG_ERROR("[TestManager] FAILED: {}", testPtr->GetName());
            }
        }

        LOG_INFO("[TestManager] Results: {} passed, {} failed", passed, failed);
    }

    IntrusivePtr<ITest> TestManager::CreateTestByType(TestType type) {
        switch (type) {
            case TestType::PoolStringTest:
                return BECore::New<Tests::PoolStringTest>();
            case TestType::FormatTest:
                return BECore::New<Tests::FormatTest>();
            case TestType::EnumUtilsTest:
                return BECore::New<Tests::EnumUtilsTest>();
            case TestType::ReflectionTest:
                return BECore::New<ReflectionTest>();
            default:
                return {};
        }
    }

}  // namespace BECore
