#include "ApplicationConfig.h"

#include <BECore/GameManager/CoreManager.h>
#include <Generated/ApplicationConfig.gen.hpp>

namespace BECore {

    ApplicationConfigLoad LoadApplicationConfig(IDeserializer& deserializer) {
        ApplicationConfigLoad load;
        load.found = true;
        deserializer.SetStrictValues(true);
        load.config.Deserialize(deserializer);
        load.errors = deserializer.GetErrors();
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
