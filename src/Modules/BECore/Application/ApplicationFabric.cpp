#include "ApplicationFabric.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/IApplicationFabric.h>
#include <BECore/Reflection/AbstractFactory.h>

namespace BECore {

    bool ApplicationFabric::Create(const ApplicationConfig& config, const ApplicationOptions& options) {
        if (config._type.empty()) {
            LOG_ERROR("ApplicationConfig: no type");
            return false;
        }

        auto fabric = AbstractFactory<IApplicationFabric>::GetInstance().Create(eastl::string_view(config._type.data(), config._type.size()));
        if (!fabric) {
            LOG_ERROR("ApplicationConfig: unknown type '{}'"_format(config._type));
            return false;
        }

        return fabric->Create(config, options);
    }

}  // namespace BECore
