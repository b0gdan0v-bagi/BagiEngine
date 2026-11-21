#pragma once

#include <BECore/Cook/ContentCooker.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <filesystem>

namespace BECore {

    /// What the cook did to one file
    CORE_ENUM(CookOutcome, uint8_t, Cooked, Copied, Failed)

    /// One file of a source layer and what became of it
    struct CookRow {
        size_t layer = 0;  ///< Position in `FileSystem::GetMountLayers("config")`, 0 the lowest
        String path;       ///< Relative to the layer's `config` folder, with `/`
        CookOutcome outcome = CookOutcome::Copied;
        Vector<String> messages;  ///< Why a file failed, one finding per entry (`<path in the document>: <message>`); empty otherwise
    };

    /**
     * @brief The result of `CookContent`: one row per file, in the order the cook walked them (layers lowest first, paths sorted)
     *
     * A failure that belongs to no file (an unusable output folder) is a `Failed` row with an empty path.
     */
    struct CookReport {
        Vector<CookRow> rows;

        /// True when no row failed: a green cook is a readable one
        [[nodiscard]] bool Ok() const;

        [[nodiscard]] size_t Count(CookOutcome outcome) const;

        /// The report for the console: one line per cooked or copied file, one per finding of a failed file, and a summary line
        [[nodiscard]] String ToText() const;
    };

    /// The files of @p layer relative to it, sorted by their generic path; dot files (`.DS_Store`) are not content. The reason goes to @p error when the folder cannot be listed.
    /// What the cook and its check (`VerifyCooked`) both walk, so they cannot disagree on which files exist
    Vector<std::filesystem::path> ListContentFiles(const std::filesystem::path& layer, String& error);

    /**
     * @brief Cooks the `config` folders of @p fs into `<outDir>/layer<k>/config/`, one output folder per source layer (k = 0 for the lowest)
     *
     * Each layer is walked recursively in sorted order, so the report and the bytes are the same for the same inputs. The output keeps
     * the relative paths, and a cooked layer replaces its source layer in the same place: the upper layer still wins, and the rule of
     * one stem per layer is the one of `ConfigManager`, with no merge step.
     *
     * - A `.xml` whose root element is `SceneManager` (a scene config) or `node` (a node file) is cooked by what the root says, before the
     *   registry is asked: the scene with its node references expanded as `LoadSceneManager` expands them (the node loader sees every layer of
     *   @p fs, `sourceRef` is kept), the node as the `SceneNode` it deserializes to. A content version that is not `kContentVersion`, an
     *   expansion error, an error or a warning of the read fails the file. A relative path is kept, so `<node ref="nodes/X"/>` finds
     *   `nodes/X.bin`.
     * - A `.xml` whose stem has a cooker in @p registry is parsed, read under `DiagnosticsMode::Error` (and strict values when the
     *   cooker asks) and written as `<stem>.bin` with a content stamp. Any finding, a file that does not parse or a cooker that wrote
     *   nothing makes the file `Failed` and no `.bin` is left for it; its `.xml` is never copied (a `.xml` beside a `.bin` of one stem
     *   is an error of `ConfigManager`).
     * - Every other file is copied byte for byte and listed as `Copied`: a config no cooker claims (a by-hand reader, a game's own),
     *   other file types. Files whose name starts with a dot are left out.
     * - A `.xml` and a `.bin` of one stem in one source layer is `Failed`, as `ConfigManager` refuses it.
     *
     * The cook owns `<outDir>/layer*`: it removes them first, so a file of an earlier cook that is gone from the source is gone from
     * the output. An @p outDir inside a `config` or `assets` layer (it would be scanned as content) or a layer inside @p outDir (the
     * removal would delete the source) is refused before anything is touched.
     *
     * @param registry The cookers; the engine's own is `ContentCookRegistry::GetInstance()`
     */
    CookReport CookContent(const FileSystem& fs, const std::filesystem::path& outDir, const ContentCookRegistry& registry);

}  // namespace BECore
