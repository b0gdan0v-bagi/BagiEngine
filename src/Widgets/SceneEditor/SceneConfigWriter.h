#pragma once

#include <BECore/Scene/SceneNodeFiles.h>

namespace BECore {

    class Scene;

    /**
     * @brief Write @p scene into the document of SceneConfig, replacing only its own `<scene>` element.
     *
     * The element is matched by its `name` attribute (an absent attribute reads as empty, so a nameless scene matches a
     * nameless element). The new element takes the place of the old one; the other `<scene>` elements, their order and the
     * comments around them are not touched. A scene without an element is appended to `<scenes>`, and a missing `<scenes>`
     * is created.
     *
     * The attributes of the element come from Scene::Serialize (so a new reflected field of Scene is saved without a
     * change here) and `type` from the scene's type name. The `<nodes>` are written from the live hierarchy, as
     * Scene::PrepareForSave() leaves it: `name`, `layer` when it is not 0, and the components through IComponent::Serialize.
     * Calls Scene::PrepareForSave() itself.
     *
     * A top-level node that came from a node file (SceneNode::GetSourceRef(), see Scene/SceneNodeFiles.h) is written as its
     * `<node ref>` with only the overrides that differ from the file (BuildNodeReference, Scene/SceneNodeInstance.h), so the
     * split of the file survives a save. When that is not possible (no @p loader, the file is gone or changed, the instance
     * gained, lost or reordered a component, the reference would not load back as the live node) the node is written inline:
     * its data is kept, its link to the file is not, and one line goes to @p notes. The node file itself is never edited.
     *
     * Needs no ImGui, and the node files are read only through @p loader (no CoreManager here), so a test can run it on a document of its own.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`, XmlNode::GetPugiNode() of the config)
     * @param scene Scene to write
     * @param loader Reads the node files of the instances; null writes every instance inline
     * @param notes Receives a line `node 'X': written inline, <reason>` for every instance written inline; may be null
     * @return false when @p configRoot is not a valid node (nothing is written); true when the element was written
     */
    bool WriteSceneToConfig(pugi::xml_node configRoot, Scene& scene, const NodeFileLoader* loader = nullptr, eastl::vector<eastl::string>* notes = nullptr);

}  // namespace BECore
