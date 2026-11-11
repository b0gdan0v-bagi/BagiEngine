#pragma once

namespace BECore {

    /**
     * @brief The format version of the content files, and the check a loader makes
     *
     * A content file is a file of a `config/` folder whose root element is one of kContentRoots: `SceneManager` (a
     * `SceneConfig.xml`) or `node` (a node file). Its root may carry `contentVersion="N"`; no attribute is version 1, the
     * format before versions. The engine reads exactly kContentVersion: an older file is rewritten by
     * `tools/migrate/migrate.py` (`tools/migrate/CLAUDE.md`), a newer one needs a newer engine.
     *
     * Pure pugixml, no engine services. `tools/migrate/steps/` holds the same number as the highest `to_version` of its
     * engine track; a pytest compares the two.
     */

    /// The version of the content format this engine reads; bumped by every change that needs a migration step
    inline constexpr uint32_t kContentVersion = 1;

    /// The version of a file that has no `contentVersion`
    inline constexpr uint32_t kUnversionedContentVersion = 1;

    /// Attribute of the root element that holds the version
    inline constexpr eastl::string_view kContentVersionAttribute = "contentVersion";

    /// Root elements of the files that carry a version
    inline constexpr eastl::string_view kSceneConfigRootName = "SceneManager";
    inline constexpr eastl::string_view kNodeFileRootName = "node";

    /// True when a file whose root element is called @p rootName is a content file
    bool IsContentRoot(eastl::string_view rootName);

    /**
     * @brief The version of the content file whose root element is @p root
     *
     * @return the number (no attribute is kUnversionedContentVersion), or the reason when the attribute is not a
     * non-negative whole number: a malformed value is neither older nor newer than the engine
     */
    std::expected<uint32_t, eastl::string> GetContentVersion(pugi::xml_node root);

    /**
     * @brief Ok when the version of @p root is kContentVersion, else the sentence a loader logs
     *
     * Older: `<what>: content version N, this engine reads M. Run: python3 engine/tools/migrate/migrate.py <your game
     * folder>`; newer: `<what>: content version N is newer than this engine reads (M): update the engine`; malformed: the
     * reason of GetContentVersion after `<what>`.
     *
     * @param what Names the file in the message
     */
    std::expected<void, eastl::string> CheckContentVersion(pugi::xml_node root, eastl::string_view what);

}  // namespace BECore
