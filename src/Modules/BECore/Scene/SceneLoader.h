#pragma once

#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNodeFiles.h>

namespace BECore {

    /// The name of the config that holds the scenes of a game (`config/SceneConfig.xml`)
    inline constexpr eastl::string_view kSceneConfigName = "SceneConfig";

    /**
     * @brief Builds the SceneManager of a `SceneConfig` whose scenes may reference node files (`<node ref="..."/>`)
     *
     * The document of @p configRoot is copied, the references are expanded in the copy (ExpandNodeReferences) and the copy
     * is deserialized, so the cached config of ConfigManager is never changed and what reads the scene afterwards (canvases,
     * `ui_tree`, the validator, the draw order) sees the same tree as for a scene written inline. Every expansion error and
     * every deserializer error is one LOG_ERROR; what could be read is kept. An invalid @p configRoot gives a manager with
     * default values, as MakeFromConfig does for a missing config.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`)
     * @param loader Reads the node file of a reference
     * @param configName Name used in the log messages
     */
    IntrusivePtrAtomic<SceneManager> LoadSceneManager(const XmlNode& configRoot, const NodeFileLoader& loader, eastl::string_view configName = kSceneConfigName);

    /// LoadSceneManager of the config `SceneConfig` of ConfigManager, with the node files found through the layered FileSystem (`config/<ref>.xml`)
    IntrusivePtrAtomic<SceneManager> LoadSceneManagerFromConfig();

}  // namespace BECore
