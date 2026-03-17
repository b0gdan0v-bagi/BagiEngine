#include "ApplicationFabric.h"

#include <BECore/Application/IApplicationFabric.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/AbstractFactory.h>

namespace BECore {

    bool ApplicationFabric::Create() {
        const auto configNode = CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern);

        if (!configNode) {
            return false;
        }

        const auto typeOpt = configNode.ParseAttribute<eastl::string_view>("type");
        if (!typeOpt) {
            return false;
        }

        auto fabric = AbstractFactory<IApplicationFabric>::GetInstance().Create(*typeOpt);
        if (!fabric) {
            return false;
        }

        return fabric->Create(configNode);
    }

}  // namespace BECore
