#include "TestManager.h"

#include <BECore/Config/ConfigFactory.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/Tests/TestList.h>

namespace BECore {

    void TestManager::RunAllTests() {
        const auto list = MakeFromConfig<Tests::TestList>(ConfigNames::tests.View());
        const auto& tests = list->tests;

        if (tests.empty()) {
            LOG_INFO("[TestManager] No runtime tests enabled in TestsConfig");
            return;
        }

        int passed = 0;
        int failed = 0;

        for (const auto& test : tests) {
            LOG_INFO("[TestManager] Running: {}", test->GetName());

            if (test->Run()) {
                ++passed;
                LOG_INFO("[TestManager] PASSED: {}", test->GetName());
            } else {
                ++failed;
                LOG_ERROR("[TestManager] FAILED: {}", test->GetName());
            }
        }

        LOG_INFO("[TestManager] Results: {} passed, {} failed", passed, failed);
    }

}  // namespace BECore
