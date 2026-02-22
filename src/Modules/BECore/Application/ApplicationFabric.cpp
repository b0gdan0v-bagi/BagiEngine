#include "ApplicationFabric.h"

#include <BECore/GameManager/CoreManager.h>

#include <Generated/EnumApplicationFabric.gen.hpp>

namespace BECore {

    bool ApplicationFabric::Create() {
        const auto configNode = CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern);

        if (!configNode) {
            return false;
        }

        const auto typeOpt = configNode.ParseAttribute<ApplicationFabricType>("type");
        if (!typeOpt) {
            return false;
        }

        auto fabric = ApplicationFabricFactory::Create(*typeOpt);
        if (!fabric) {
            return false;
        }

        return fabric->Create(configNode);
    }

}  // namespace BECore
