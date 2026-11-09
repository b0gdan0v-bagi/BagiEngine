#pragma once

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
     * A scene whose existing element has a `<node ref>` child (a node kept in a node file, see Scene/SceneNodeFiles.h) is
     * not written: the live tree is flat, so saving would inline every reference and undo the split of the file. Nothing is
     * changed then. Until the editor can save references, the file of such a scene is edited by hand. A reference in
     * another scene does not matter.
     *
     * Needs no ImGui and no engine services, so a test can run it on a document of its own.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`, XmlNode::GetPugiNode() of the config)
     * @param scene Scene to write
     * @return false when @p configRoot is not a valid node or the scene's element has node references (nothing is
     * written); true when the element was written
     */
    bool WriteSceneToConfig(pugi::xml_node configRoot, Scene& scene);

}  // namespace BECore
