#pragma once

#include <BECore/Scene/SceneNodeFiles.h>

namespace BECore {

    class Scene;

    /**
     * @brief Writes @p scene into the document of SceneConfig, replacing only its own `<scene>` element (matched by `name`;
     * an absent attribute reads as empty).
     *
     * The other `<scene>` elements, their order and the comments around them are not touched; a scene without an element is
     * appended to `<scenes>`, a missing `<scenes>` is created. The replaced `<scene>` is rebuilt from the live scene, so the
     * comments **inside** it are lost. The attributes come from Scene::Serialize (a new reflected
     * field of Scene is saved without a change here), `type` from the scene's type name, the `<nodes>` from the live
     * hierarchy as Scene::PrepareForSave() (called here) leaves it.
     *
     * A top-level node that came from a node file (SceneNode::GetSourceRef()) is written as its `<node ref>` with only the
     * overrides that differ from the file (BuildNodeReference, Scene/SceneNodeInstance.h). When that is not possible (no
     * @p loader, the file is gone or changed, the instance gained, lost or reordered a component, the reference would not
     * load back as the live node) the node is written inline: its data is kept, its link to the file is not, and one line
     * goes to @p notes. The node file itself is never edited.
     *
     * Needs no ImGui; the node files are read only through @p loader (no CoreManager), so a test can run it on a document
     * of its own.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`, XmlNode::GetPugiNode() of the config)
     * @param scene Scene to write
     * @param loader Reads the node files of the instances; null writes every instance inline
     * @param notes Receives a line `node 'X': written inline, <reason>` for every instance written inline; may be null
     * @return false when @p configRoot is not a valid node (nothing is written); true when the element was written
     */
    bool WriteSceneToConfig(pugi::xml_node configRoot, Scene& scene, const NodeFileLoader* loader = nullptr, Vector<String>* notes = nullptr);

}  // namespace BECore
