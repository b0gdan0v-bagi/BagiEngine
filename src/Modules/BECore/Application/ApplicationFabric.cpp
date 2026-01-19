#include "ApplicationFabric.h"

#include <BECore/Application/ApplicationSystemType.h>
#include <BECore/Config/XmlConfig.h>
#include <CoreSDL/ApplicationSDLFabric.h>

namespace BECore {

    bool ApplicationFabric::Create() {

        XmlConfig config = XmlConfig::Create();
        constexpr eastl::string_view configPath = "config/ApplicationConfig.xml";
        if (!config.LoadFromVirtualPath(configPath)) {
            return false;
        }

        ApplicationSystemType type = config.GetRoot().ParseAttribute<ApplicationSystemType>("type").value_or(ApplicationSystemType::None);

        switch (type) {
            case ApplicationSystemType::SDL3:
                if (!ApplicationSDLFabric::Create(config)) {
                    return false;
                }
                break;
            default:
                return false;
        }

        return true;
    }

}  // namespace BECore
