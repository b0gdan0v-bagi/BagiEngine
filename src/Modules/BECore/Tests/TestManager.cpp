#include "TestManager.h"

#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>

#include <Generated/EnumTest.gen.hpp>

namespace BECore {

    void TestManager::RunAllTests() {
        // Получаем конфиг через ConfigManager
        const auto rootNode = CoreManager::GetConfigManager().GetConfig("TestsConfig"_intern);

        if (!rootNode) {
            LOG_ERROR("[TestManager] Config not found: TestsConfig");
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

            // Use auto-generated TestFactory to create test instances
            auto testPtr = Tests::TestFactory::GetInstance().Create(*testType);
            if (!testPtr) {
                LOG_ERROR("[TestManager] Failed to create test of type: {}"_format(
                    Tests::TestFactory::GetInstance().GetTypeName(*testType)));
                continue;
            }

            LOG_INFO("[TestManager] Running: {}"_format(testPtr->GetName()));

            if (testPtr->Run()) {
                ++passed;
                LOG_INFO("[TestManager] PASSED: {}"_format(testPtr->GetName()));
            } else {
                ++failed;
                LOG_ERROR("[TestManager] FAILED: {}"_format(testPtr->GetName()));
            }
        }

        LOG_INFO("[TestManager] Results: {} passed, {} failed"_format(passed, failed));
    }

}  // namespace BECore
