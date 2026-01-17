#include "TestManager.h"

#include <BECore/Tests/PoolStringTest.h>
#include <BECore/Tests/FormatTest.h>
#include <BECore/Config/XmlConfig.h>
#include <iostream>

namespace BECore {

    void TestManager::RunAllTests() {
        XmlConfig config = XmlConfig::Create();
        constexpr std::string_view configPath = "config/TestsConfig.xml";

        if (!config.LoadFromVirtualPath(configPath)) {
            std::cout << "[TestManager] Config not found: " << configPath << std::endl;
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

            std::cout << "[TestManager] Running: " << testPtr->GetName() << std::endl;

            if (testPtr->Run()) {
                ++passed;
                std::cout << "[TestManager] PASSED: " << testPtr->GetName() << std::endl;
            } else {
                ++failed;
                std::cout << "[TestManager] FAILED: " << testPtr->GetName() << std::endl;
            }
        }

        std::cout << "[TestManager] Results: " << passed << " passed, " << failed << " failed" << std::endl;
    }

    IntrusivePtr<ITest> TestManager::CreateTestByType(TestType type) {
        switch (type) {
            case TestType::PoolStringTest:
                return BECore::New<Tests::PoolStringTest>();
            case TestType::FormatTest:
                return BECore::New<Tests::FormatTest>();
            default:
                return {};
        }
    }

}  // namespace BECore
