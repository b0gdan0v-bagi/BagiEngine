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

    ApplicationConfigLoad LoadApplicationConfig() {
        const auto deserializer = CoreManager::GetConfigManager().CreateDeserializer("ApplicationConfig"_intern);
        if (!deserializer) {
            return {};
        }
        return LoadApplicationConfig(*deserializer);
    }

}  // namespace BECore
