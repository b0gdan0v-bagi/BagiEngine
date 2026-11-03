#include "TestManager.h"

#include <BECore/Config/ConfigFactory.h>
#include <BECore/Tests/TestList.h>

namespace BECore {

    void TestManager::RunAllTests() {
        const auto list = MakeFromConfig<Tests::TestList>("TestsConfig");
        const auto& tests = list->tests;

        if (tests.empty()) {
            LOG_INFO("[TestManager] No runtime tests enabled in TestsConfig");
            return;
        }

        int passed = 0;
        int failed = 0;

        for (const auto& test : tests) {
            LOG_INFO("[TestManager] Running: {}"_format(test->GetName()));

            if (test->Run()) {
                ++passed;
                LOG_INFO("[TestManager] PASSED: {}"_format(test->GetName()));
            } else {
                ++failed;
                LOG_ERROR("[TestManager] FAILED: {}"_format(test->GetName()));
            }
        }

        LOG_INFO("[TestManager] Results: {} passed, {} failed"_format(passed, failed));
    }

}  // namespace BECore
