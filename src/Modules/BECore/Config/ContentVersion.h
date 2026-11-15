#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>
#include <pugixml.hpp>

namespace BECore {

    /**
     * @brief The format version of the content files, and the check a loader makes
     *
     * A content file is a file of a `config/` folder whose root element is kSceneConfigRootName (`SceneManager`, a
     * `SceneConfig.xml`) or kNodeFileRootName (`node`, a node file), as IsContentRoot says. Its root may carry
     * `contentVersion="N"`; no attribute is version 1, the format before versions. The engine reads exactly
     * kContentVersion: an older file is rewritten by `tools/migrate/migrate.py` (`tools/migrate/CLAUDE.md`), a newer one
     * needs a newer engine.
     *
     * Pure pugixml, no engine services. The highest `to_version` of the engine track of `tools/migrate/steps/` must equal
     * kContentVersion (a pytest compares them).
     */

    /// Bumped by every change that needs a migration step (`tools/migrate/CLAUDE.md`)
    inline constexpr uint32_t kContentVersion = 5;

    /// What a file without `contentVersion` is
    inline constexpr uint32_t kUnversionedContentVersion = 1;

    inline constexpr StringView kContentVersionAttribute = "contentVersion";

    /// The root elements of content files; a new one is added with the step that first rewrites such a file
    inline constexpr StringView kSceneConfigRootName = "SceneManager";
    inline constexpr StringView kNodeFileRootName = "node";

    bool IsContentRoot(StringView rootName);

    /**
     * @brief The version of the content file whose root element is @p root
     *
     * @return The number, or the reason when the attribute is not a non-negative whole number: a malformed value is
     * neither older nor newer than the engine
     */
    std::expected<uint32_t, String> GetContentVersion(pugi::xml_node root);

    /**
     * @brief Ok when the version of @p root is kContentVersion, else the sentence a loader logs
     *
     * Older: `<what>: content version N, this engine reads M. Run: python3 engine/tools/migrate/migrate.py <your game
     * folder>`; newer: `<what>: content version N is newer than this engine reads (M): update the engine`; malformed: the
     * reason of GetContentVersion after `<what>`.
     *
     * @param what Names the file in the message
     */
    std::expected<void, String> CheckContentVersion(pugi::xml_node root, StringView what);

}  // namespace BECore
