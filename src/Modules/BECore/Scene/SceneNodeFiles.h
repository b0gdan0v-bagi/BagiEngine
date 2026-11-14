#pragma once

#include <BECore/FileSystem/FileSystem.h>

namespace BECore {

    /**
     * @brief Node files: a node of a scene kept in a file of its own and referenced by name from `SceneConfig.xml`.
     *
     * Inside `<nodes>` of a `<scene>` the element `<node ref="nodes/MahjongHud"/>` stands for the node in
     * `config/nodes/MahjongHud.xml`, whose document element is one `<node name=".." layer="..">` with `<components>`,
     * exactly as a node inside SceneConfig. The expansion replaces the reference by a copy of that element in the same
     * place, so the scene order (and with it the draw order and the input order) is the order of the references. The copy
     * loses the `contentVersion` attribute of the file's root: the live scene never sees it.
     *
     * A reference may override values of its instance (see ApplyNodeOverrides): the attributes `name` and `layer` of the
     * node and, inside `<components>`, attributes and child elements of the node file's components, matched by `type`.
     * It cannot add or remove a component or a field.
     *
     * One reference also has a single form, ExpandNodeReference, that the scene expansion and the runtime instantiation of
     * a node (`Scene::InstantiateNode`) share, so the overrides can never differ between them.
     *
     * Everything here works on pugixml documents: no CoreManager, no scene, no renderer. A bad reference never stops a
     * game: it is one error in NodeExpansion::errors, the reference element is dropped and the rest of the scene stays.
     */

    /// Name of the element that is a node and of the one that may be a reference
    inline constexpr StringView kNodeElementName = "node";

    /// Attribute of `<node>` that makes it a reference
    inline constexpr StringView kNodeRefAttribute = "ref";

    /// Attributes of a reference that replace the same attribute of the node file's `<node>`: the reflected fields of
    /// SceneNode (`_name`, `_layer`). When SceneNode gets a field, extend this list.
    inline constexpr std::array<StringView, 2> kNodeOverrideAttributes = {"name", "layer"};

    /// Attribute that the expansion may set on the copy of a node file's `<node>`: the reference it came from. The live
    /// SceneNode keeps it (`GetSourceRef`) so the scene editor can write the node back as a reference
    inline constexpr StringView kNodeSourceAttribute = "sourceRef";

    /// The only child element of a reference, and its children: one per component to override
    inline constexpr StringView kComponentsElementName = "components";
    inline constexpr StringView kComponentElementName = "component";

    /// Attribute of `<component>` that names its type (in the node file and in the override)
    inline constexpr StringView kComponentTypeAttribute = "type";

    /// Mount point of the node files; a reference `a/b` is the file `config/a/b.xml`
    inline constexpr StringView kNodeFilesMountPoint = "config";

    /// Extension of a node file
    inline constexpr StringView kNodeFileExtension = ".xml";

    /**
     * @brief Fills @p out with the parsed node file of a reference
     *
     * @param ref A reference that passed IsValidNodeRef (`nodes/MahjongHud`)
     * @param out Receives the document; its document element is the `<node>`
     * @param error Receives the reason when the file cannot be used
     * @return false when it cannot be read, parsed or is not a node file
     */
    using NodeFileLoader = eastl::function<bool(StringView ref, pugi::xml_document& out, eastl::string& error)>;

    /// The result of an expansion: how many references were replaced and one message per reference that was not
    struct NodeExpansion {
        size_t expanded = 0;
        eastl::vector<eastl::string> errors;  // each names the scene and the reference

        bool Ok() const {
            return errors.empty();
        }
    };

    /**
     * @brief True when @p ref is a reference: segments of `[A-Za-z0-9_-]` separated by `/`
     *
     * No empty segment, no `.` or `..`, no leading `/`, no `\`, no extension (the dot is not in the alphabet).
     */
    bool IsValidNodeRef(StringView ref);

    /**
     * @brief Merges the overrides of a node reference into the `<node>` of its file
     *
     * - The attributes `name` and `layer` of @p reference replace (or add) the same attribute of @p node; `ref` is the
     *   reference itself; any other attribute is an error.
     * - The only child element of @p reference is one `<components>`, whose only children are `<component type="..">`.
     *   Each is merged into the component of @p node with the same `type`: an attribute other than `type` replaces (or
     *   adds) the attribute of the file's component, a child element replaces, whole, the child elements of the same name
     *   (at the place of the first, appended when there is none). No deep merge.
     * - A field is overridden whole in either spelling: the file's attribute and its child elements of that name are
     *   removed first, then the spelling of the override is written, so exactly one remains.
     * - No adding and no removing: a `type` that @p node has no component of is an error. A duplicate `type`, a field
     *   of one component written in both spellings, a child other than `components` / `component`, a second `<components>`
     *   and a `component` without `type` are errors. Text and comments are ignored.
     *
     * The whole reference is checked before anything changes: on false @p node is untouched.
     *
     * @param node The `<node>` of a loaded node file
     * @param reference The `<node ref="..">` element of the scene
     * @param error Receives the reason when the reference is invalid
     * @return false when the reference is invalid or names a component @p node does not have
     */
    bool ApplyNodeOverrides(pugi::xml_node node, pugi::xml_node reference, eastl::string& error);

    /**
     * @brief Expands one `<node ref="..">` element into a copy of its file's `<node>`
     *
     * Checks IsValidNodeRef, loads the file with @p loader, merges the overrides (ApplyNodeOverrides) and leaves in @p out
     * a document whose only element is a copy of the file's `<node>` without `contentVersion`. With @p markSource the copy
     * carries `sourceRef="<ref>"` (kNodeSourceAttribute). @p reference is not changed.
     *
     * @param reference The `<node ref="..">` element, with its overrides
     * @param loader Reads the node file of the reference
     * @param markSource Set kNodeSourceAttribute on the copy
     * @param out Receives the node; empty on failure
     * @param error Receives the reason on failure
     * @return false when the reference cannot be used (invalid, file missing or wrong, invalid override)
     */
    bool ExpandNodeReference(pugi::xml_node reference, const NodeFileLoader& loader, bool markSource, pugi::xml_document& out, eastl::string& error);

    /**
     * @brief Replaces every `scenes/scene/nodes/node[@ref]` of @p configRoot by the node of its file
     *
     * The copy of the file's document element, with the overrides of the reference applied (ApplyNodeOverrides), takes
     * the place of the reference. A reference that cannot be used (invalid, a file that is missing, does not parse or is
     * not a node file, an invalid override) is removed and recorded in the result, never half applied; the other nodes of
     * the scene are kept in order. Elements without `ref` and documents without references are left as they are.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`)
     * @param loader Reads the node file of a reference
     * @param markSource Every expanded node gets kNodeSourceAttribute (what LoadSceneManager wants); tools that read the
     *        file leave it false, so the document they see is the plain inline one
     */
    NodeExpansion ExpandNodeReferences(pugi::xml_node configRoot, const NodeFileLoader& loader, bool markSource = false);

    /**
     * @brief Loader that reads `<baseDirectory>/<ref>.xml`, for tools and tests that have only a folder
     *
     * Checks IsValidNodeRef and that the document element is `node` without a `ref` attribute. It does **not** check the
     * content version (`BECore/Config/ContentVersion.h`): tools and tests with a folder read files of any version.
     */
    NodeFileLoader MakeDirectoryNodeLoader(std::filesystem::path baseDirectory);

    /**
     * @brief Loader that reads `FileSystem::ResolvePath("config/<ref>.xml")`, so the content layers apply
     *
     * A file of the game with the same relative path replaces the engine's. Same checks as MakeDirectoryNodeLoader, and a
     * file whose content version is not kContentVersion is refused (the reason names the file, the versions and the
     * migration command). @p fs must outlive the loader.
     */
    NodeFileLoader MakeFileSystemNodeLoader(const FileSystem& fs);

    /**
     * @brief Parses a `SceneConfig.xml` and expands its node references next to it (`<folder of the file>/<ref>.xml`)
     *
     * What a test or a tool that reads the file calls instead of `pugi::xml_document::load_file`. A file that does not
     * exist or does not parse is an error in the result and leaves @p out empty.
     */
    NodeExpansion LoadSceneDocument(const std::filesystem::path& sceneConfigFile, pugi::xml_document& out);

}  // namespace BECore
