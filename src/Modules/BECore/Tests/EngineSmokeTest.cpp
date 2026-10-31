#include "EngineSmokeTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <Generated/EngineSmokeTest.gen.hpp>

namespace BECore::Tests {

    bool EngineSmokeTest::Run() {
        // ConfigManager loaded config/ at startup; this test itself was listed in one of those files
        const auto hasConfig = [](PoolString name) {
            if (CoreManager::GetConfigManager().HasConfig(name)) {
                return true;
            }
            LOG_ERROR("[EngineSmokeTest] Config '{}' was not loaded"_format(name.ToStringView()));
            return false;
        };

        return hasConfig("TestsConfig"_intern) && hasConfig("LoggerConfig"_intern) && hasConfig("AssertHandlersConfig"_intern);
    }

}  // namespace BECore::Tests
