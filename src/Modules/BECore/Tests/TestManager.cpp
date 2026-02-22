#include "TestManager.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/FieldSerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Generated/ITest.gen.hpp>
#include <Generated/EnumTest.gen.hpp>

namespace BECore {

    void TestManager::RunAllTests() {
        eastl::vector<IntrusivePtrAtomic<Tests::ITest>> tests;

        auto root = CoreManager::GetConfigManager().GetConfig("TestsConfig");
        if (root) {
            XmlDeserializer d;
            d.LoadFromXmlNode(root);
            DeserializeField(d, "tests", tests);
        }

        if (tests.empty()) {
            LOG_WARNING("[TestManager] No tests loaded from TestsConfig");
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
