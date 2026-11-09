#pragma once

#include <BECore/FileSystem/FileSystem.h>

namespace BECore {

    /**
     * @brief Node files: a node of a scene kept in a file of its own and referenced by name from `SceneConfig.xml`.
     *
     * Inside `<nodes>` of a `<scene>` the element `<node ref="nodes/MahjongHud"/>` stands for the node in
     * `config/nodes/MahjongHud.xml`, whose document element is one `<node name=".." layer="..">` with `<components>`,
     * exactly as a node inside SceneConfig. The expansion replaces the reference by a copy of that element in the same
     * place, so the scene order (and with it the draw order and the input order) is the order of the references.
     *
     * Everything here works on pugixml documents: no CoreManager, no scene, no renderer. A bad reference never stops a
     * game: it is one error in NodeExpansion::errors, the reference element is dropped and the rest of the scene stays.
     */

    /// Name of the element that is a node and of the one that may be a reference
    inline constexpr eastl::string_view kNodeElementName = "node";

    /// Attribute of `<node>` that makes it a reference
    inline constexpr eastl::string_view kNodeRefAttribute = "ref";

    /// Mount point of the node files; a reference `a/b` is the file `config/a/b.xml`
    inline constexpr eastl::string_view kNodeFilesMountPoint = "config";

    /// Extension of a node file
    inline constexpr eastl::string_view kNodeFileExtension = ".xml";

    /**
     * @brief Fills @p out with the parsed node file of a reference
     *
     * @param ref A reference that passed IsValidNodeRef (`nodes/MahjongHud`)
     * @param out Receives the document; its document element is the `<node>`
     * @param error Receives the reason when the file cannot be used
     * @return false when it cannot be read, parsed or is not a node file
     */
    using NodeFileLoader = eastl::function<bool(eastl::string_view ref, pugi::xml_document& out, eastl::string& error)>;

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
    bool IsValidNodeRef(eastl::string_view ref);

    /**
     * @brief Replaces every `scenes/scene/nodes/node[@ref]` of @p configRoot by the node of its file
     *
     * The copy of the file's document element takes the place of the reference. A reference that cannot be used (invalid,
     * with an attribute other than `ref` or with a child element, a file that is missing, does not parse or is not a node
     * file) is removed and recorded in the result; the other nodes of the scene are kept in order. Elements without `ref`
     * and documents without references are left as they are.
     *
     * @param configRoot Document element of SceneConfig (`<SceneManager>`)
     * @param loader Reads the node file of a reference
     */
    NodeExpansion ExpandNodeReferences(pugi::xml_node configRoot, const NodeFileLoader& loader);

    /**
     * @brief Loader that reads `<baseDirectory>/<ref>.xml`, for tools and tests that have only a folder
     *
     * Checks IsValidNodeRef and that the document element is `node` without a `ref` attribute.
     */
    NodeFileLoader MakeDirectoryNodeLoader(std::filesystem::path baseDirectory);

    /**
     * @brief Loader that reads `FileSystem::ResolvePath("config/<ref>.xml")`, so the content layers apply
     *
     * A file of the game with the same relative path replaces the engine's. Same checks as MakeDirectoryNodeLoader.
     * @p fs must outlive the loader.
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
