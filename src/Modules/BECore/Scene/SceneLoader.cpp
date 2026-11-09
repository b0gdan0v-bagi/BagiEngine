#include "SceneLoader.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    namespace {

        constexpr eastl::string_view kSceneConfigName = "SceneConfig";

    }  // namespace

    IntrusivePtrAtomic<SceneManager> LoadSceneManager(const XmlNode& configRoot, const NodeFileLoader& loader, eastl::string_view configName) {
        auto manager = New<SceneManager>();
        if (!configRoot) {
            return manager;
        }

        // The copy outlives the deserializer; the cached document of ConfigManager stays as it is
        pugi::xml_document expanded;
        expanded.append_copy(configRoot.GetPugiNode());
        const NodeExpansion expansion = ExpandNodeReferences(expanded.document_element(), loader);
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

    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromConfig() {
        return LoadSceneManager(CoreManager::GetConfigManager().GetConfig(kSceneConfigName), MakeFileSystemNodeLoader(CoreManager::GetFileSystem()), kSceneConfigName);
    }

}  // namespace BECore
