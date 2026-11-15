#include "ApplicationConfig.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <Generated/ApplicationConfig.gen.hpp>

namespace BECore {

    ApplicationConfigLoad LoadApplicationConfig(IDeserializer& deserializer) {
        ApplicationConfigLoad load;
        load.found = true;
        deserializer.SetStrictValues(true);
        load.config.Deserialize(deserializer);
        deserializer.FinishLoad();
        load.errors = deserializer.GetErrors();
        load.warnings = deserializer.GetWarnings();
        return load;
    }

    ApplicationConfigLoad LoadApplicationConfig(PoolString configName) {
        const auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(configName);
        if (!deserializer) {
            return {};
        }
        return LoadApplicationConfig(*deserializer);
    }

    ApplicationConfigLoad LoadApplicationConfig() {
        return LoadApplicationConfig("ApplicationConfig"_intern);
    }

}  // namespace BECore
