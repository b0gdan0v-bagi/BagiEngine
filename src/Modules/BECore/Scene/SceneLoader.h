#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ConfigManager;

    /// The name of the config that holds the scenes of a game (`config/SceneConfig.xml`)
    inline constexpr StringView kSceneConfigName = "SceneConfig";

    /// The errors and the warnings (findings) the deserializer of a scene collected, and the node references that could not be expanded; the log has them too
    struct SceneLoadFindings {
        Vector<String> expansionErrors;  ///< One per node reference that was dropped (NodeExpansion::errors); the cook of a scene fails on any
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
     * @param findings When not null, receives the expansion errors and the errors and warnings of the deserializer (a test, or a check of the files)
     */
    IntrusivePtrAtomic<SceneManager> LoadSceneManager(const XmlNode& configRoot, NodeFileLoaderRef loader, StringView configName = kSceneConfigName, SceneLoadFindings* findings = nullptr);

    /**
     * @brief LoadSceneManager of a root whose content version is checked first
     *
     * A root that is not kContentVersion (`BECore/Config/ContentVersion.h`) is one LOG_ERROR with the message of
     * CheckContentVersion and gives an empty SceneManager: no scene is read from a format the engine does not know.
     */
    IntrusivePtrAtomic<SceneManager> LoadCheckedSceneManager(const XmlNode& configRoot, NodeFileLoaderRef loader, StringView configName = kSceneConfigName);

    /// The type a cooked `SceneConfig` is stamped with (what the cook of the file reads it as)
    inline constexpr StringView kSceneManagerRootType = "SceneManager";

    /**
     * @brief Builds the SceneManager of a cooked (binary) `SceneConfig`
     *
     * The stamp of @p reader (CheckContentStamp) must be one of this program's `SceneManager`; any other is one LOG_ERROR and an
     * empty manager. The document is read as it is: the cook has expanded the node references (a `ref` left in it is not
     * looked up) and checked the content version, which is why neither is done here. Errors are one LOG_ERROR each as in
     * LoadSceneManager; a binary document reports no findings, so @p findings gets only what the reader found.
     *
     * @param reader Positioned at the root scope of the file, as ConfigManager::CreateDeserializer gives it
     * @param configName Name used in the log messages
     * @param findings When not null, receives the errors and warnings of the reader
     */
    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromBinary(IDeserializer& reader, StringView configName = kSceneConfigName, SceneLoadFindings* findings = nullptr);

    /// LoadCheckedSceneManager of the config `SceneConfig` of ConfigManager, with the node files found through the layered FileSystem (`config/<ref>.xml` or `.bin`, checked too); a binary
    /// `SceneConfig` goes through LoadSceneManagerFromBinary
    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromConfig();

    /// The same over a ConfigManager and a FileSystem of the caller's (a test or a tool with content folders of its own); both must outlive the call
    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromConfig(const ConfigManager& configs, const FileSystem& fs);

}  // namespace BECore
