#include "ApplicationFabric.h"

#include <Application/Application.h>
#include <Core/Config/XmlConfig.h>
#include <Core/FileSystem/FileSystem.h>
#include <CoreSDL/ApplicationSDLFabric.h>

namespace Core {

    bool ApplicationFabric::Create() {

        XmlConfig config;
        constexpr std::string_view configPath = "config/ApplicationConfig.xml";
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

}  // namespace Core
