#include "ApplicationFabric.h"

#include <BECore/Application/ApplicationSystemType.h>
#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>
#include <CoreSDL/ApplicationSDLFabric.h>

namespace BECore {

    bool ApplicationFabric::Create() {

        // Получаем конфиг через ConfigManager
        const auto rootNode = CoreManager::GetConfigManager().GetConfig("ApplicationConfig"_intern);
        
        if (!rootNode) {
            return false;
        }

        ApplicationSystemType type = rootNode.ParseAttribute<ApplicationSystemType>("type").value_or(ApplicationSystemType::None);

        switch (type) {
            case ApplicationSystemType::SDL3: {
                // Создаём временный XmlConfig для передачи в ApplicationSDLFabric
                // TODO: В будущем можно передавать XmlNode напрямую
                XmlConfig config = XmlConfig::Create();
                if (config.LoadFromVirtualPath("config/ApplicationConfig.xml")) {
                    if (!ApplicationSDLFabric::Create(config)) {
                        return false;
                    }
                } else {
                    return false;
                }
                break;
            }
            default:
                return false;
        }

        return true;
    }

}  // namespace BECore
