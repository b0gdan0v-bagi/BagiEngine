#include "SceneLoader.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Reflection/XmlDeserializer.h>

namespace BECore {

    IntrusivePtr<SceneManager> LoadSceneManager(const XmlNode& configRoot, NodeFileLoaderRef loader, StringView configName, SceneLoadFindings* findings) {
        auto manager = New<SceneManager>();
        if (!configRoot) {
            return manager;
        }

        // The copy outlives the deserializer; the cached document of ConfigManager stays as it is
        pugi::xml_document expanded;
        expanded.append_copy(configRoot.GetPugiNode());
        const NodeExpansion expansion = ExpandNodeReferences(expanded.document_element(), loader, true);
        for (const String& error : expansion.errors) {
            LOG_ERROR(Format("Config '{}': {}", configName, error).c_str());
        }
        if (findings != nullptr) {
            findings->expansionErrors = expansion.errors;
        }

        XmlDeserializer deserializer;
        deserializer.SetDiagnostics(ConfigManager::GetDiagnostics());
        if (deserializer.LoadFromXmlNode(XmlNode{expanded.document_element()})) {
            // The root's contentVersion is read by CheckContentVersion and ExpandNodeReferences, not by the reflected SceneManager
            deserializer.NoteKnownKey(kContentVersionAttribute);
            manager->Deserialize(deserializer);
            deserializer.FinishLoad();
            LogDeserializeErrors(configName, deserializer);
            if (findings != nullptr) {
                findings->errors = deserializer.GetErrors();
                findings->warnings = deserializer.GetWarnings();
            }
        }
        return manager;
    }

    IntrusivePtr<SceneManager> LoadCheckedSceneManager(const XmlNode& configRoot, NodeFileLoaderRef loader, StringView configName) {
        if (configRoot) {
            if (const auto version = CheckContentVersion(configRoot.GetPugiNode(), Format("Config '{}'", configName)); !version) {
                LOG_ERROR(version.error().c_str());
                return New<SceneManager>();
            }
        }
        return LoadSceneManager(configRoot, loader, configName);
    }

    IntrusivePtr<SceneManager> LoadSceneManagerFromBinary(IDeserializer& reader, StringView configName, SceneLoadFindings* findings) {
        auto manager = New<SceneManager>();
        const auto stamp = CheckContentStamp(reader, configName);
        if (!stamp) {
            LOG_ERROR(Format("Config '{}': {}", configName, stamp.error()).c_str());
            return manager;
        }
        if (stamp->rootType != kSceneManagerRootType) {
            LOG_ERROR(Format("Config '{}' holds a '{}', not a '{}'", configName, stamp->rootType, kSceneManagerRootType).c_str());
            return manager;
        }
        manager->Deserialize(reader);
        reader.FinishLoad();
        LogDeserializeErrors(configName, reader);
        if (findings != nullptr) {
            findings->errors = reader.GetErrors();
            findings->warnings = reader.GetWarnings();
        }
        return manager;
    }

    IntrusivePtr<SceneManager> LoadSceneManagerFromConfig() {
        return LoadSceneManagerFromConfig(CoreManager::GetConfigManager(), CoreManager::GetFileSystem());
    }

    IntrusivePtr<SceneManager> LoadSceneManagerFromConfig(const ConfigManager& configs, const FileSystem& fs) {
        if (configs.IsBinaryConfig(kSceneConfigName)) {
            const UniquePtr<IDeserializer> reader = configs.CreateDeserializer(kSceneConfigName);
            if (!reader) {
                LOG_ERROR(Format("Config '{}' cannot be read", kSceneConfigName).c_str());
                return New<SceneManager>();
            }
            return LoadSceneManagerFromBinary(*reader, kSceneConfigName);
        }
        return LoadCheckedSceneManager(configs.GetConfig(kSceneConfigName), MakeFileSystemNodeLoader(fs), kSceneConfigName);
    }

}  // namespace BECore
