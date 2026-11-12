#include "SceneLoader.h"

#include <BECore/Config/ContentVersion.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    IntrusivePtrAtomic<SceneManager> LoadSceneManager(const XmlNode& configRoot, const NodeFileLoader& loader, eastl::string_view configName) {
        auto manager = New<SceneManager>();
        if (!configRoot) {
            return manager;
        }

        // The copy outlives the deserializer; the cached document of ConfigManager stays as it is
        pugi::xml_document expanded;
        expanded.append_copy(configRoot.GetPugiNode());
        const NodeExpansion expansion = ExpandNodeReferences(expanded.document_element(), loader, true);
        for (const eastl::string& error : expansion.errors) {
            LOG_ERROR(Format("Config '{}': {}", configName, error).c_str());
        }

        XmlDeserializer deserializer;
        if (deserializer.LoadFromXmlNode(XmlNode{expanded.document_element()})) {
            manager->Deserialize(deserializer);
            if (deserializer.HasErrors()) {
                LogDeserializeErrors(configName, deserializer);
            }
        }
        return manager;
    }

    IntrusivePtrAtomic<SceneManager> LoadCheckedSceneManager(const XmlNode& configRoot, const NodeFileLoader& loader, eastl::string_view configName) {
        if (configRoot) {
            if (const auto version = CheckContentVersion(configRoot.GetPugiNode(), Format("Config '{}'", configName)); !version) {
                LOG_ERROR(version.error().c_str());
                return New<SceneManager>();
            }
        }
        return LoadSceneManager(configRoot, loader, configName);
    }

    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromConfig() {
        return LoadCheckedSceneManager(CoreManager::GetConfigManager().GetConfig(kSceneConfigName), MakeFileSystemNodeLoader(CoreManager::GetFileSystem()), kSceneConfigName);
    }

}  // namespace BECore
