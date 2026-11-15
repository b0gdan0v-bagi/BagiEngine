#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /// The name of the config that holds the scenes of a game (`config/SceneConfig.xml`)
    inline constexpr StringView kSceneConfigName = "SceneConfig";

    /// The errors and the warnings (findings) the deserializer of a scene collected; the log has them too
    struct SceneLoadFindings {
        Vector<DeserializeError> errors;
        Vector<DeserializeError> warnings;
    };

    /**
     * @brief Builds the SceneManager of a `SceneConfig` whose scenes may reference node files (`<node ref="..."/>`)
     *
     * The document of @p configRoot is copied, the references are expanded in the copy (ExpandNodeReferences) and the copy
     * is deserialized, so the cached config of ConfigManager is never changed and what reads the scene afterwards sees the
     * same tree as for a scene written inline. Every expanded node remembers its file (`SceneNode::GetSourceRef`). Every expansion error and
     * every deserializer error is one LOG_ERROR and every finding of ConfigManager::GetDiagnostics() (an unknown key) one LOG_WARNING
     * (a LOG_ERROR with `--strict-config`); what could be read is kept. An invalid @p configRoot gives a manager with
     * default values, as MakeFromConfig does for a missing config.
     *
     * It does not check the content version of @p configRoot (tests and tools load inline XML of any version):
     * LoadCheckedSceneManager does.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`)
     * @param loader Reads the node file of a reference
     * @param configName Name used in the log messages
     * @param findings When not null, receives the errors and warnings of the deserializer (a test, or a check of the files)
     */
    IntrusivePtrAtomic<SceneManager> LoadSceneManager(const XmlNode& configRoot, NodeFileLoaderRef loader, StringView configName = kSceneConfigName, SceneLoadFindings* findings = nullptr);

    /**
     * @brief LoadSceneManager of a root whose content version is checked first
     *
     * A root that is not kContentVersion (`BECore/Config/ContentVersion.h`) is one LOG_ERROR with the message of
     * CheckContentVersion and gives an empty SceneManager: no scene is read from a format the engine does not know.
     */
    IntrusivePtrAtomic<SceneManager> LoadCheckedSceneManager(const XmlNode& configRoot, NodeFileLoaderRef loader, StringView configName = kSceneConfigName);

    /// LoadCheckedSceneManager of the config `SceneConfig` of ConfigManager, with the node files found through the layered FileSystem (`config/<ref>.xml`, checked too)
    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromConfig();

}  // namespace BECore
