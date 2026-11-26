#pragma once

#include <BECore/File/Path.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Widgets/EditorDocuments.h>

namespace BECore {

    class ConfigManager;
    class FileSystem;

    /**
     * @brief The `editor_state` row of a document that is a config of @p manager (the scene file, the selected config, the sprite library)
     *
     * `id` is `<kind>:<name>`. `path` is the virtual path of the config's file, `file` the real file a save writes (`FileSystem::WriteTarget`: the
     * overlay's with `--content-overlay`, also before the first save); both are empty for a config the manager does not know. No ImGui: the panels pass
     * the flags they already keep.
     */
    [[nodiscard]] EditorDocumentState MakeConfigDocument(const ConfigManager& manager, EditorDocumentKind kind, StringView name, bool dirty, PoolString config);

    /// The `config` row of a file of the `config` mount that a panel reads itself and not through the manager; @p file is where it is, or where a
    /// save would make it (both paths of the row are empty for an empty one)
    [[nodiscard]] EditorDocumentState MakeConfigFileDocument(const FileSystem& fileSystem, StringView name, bool dirty, const Path& file);

    /// The row of the node file @p ref (always dirty: it is listed only while edits wait for it), `path` as `SceneNodeFiles` resolves it
    [[nodiscard]] EditorDocumentState MakeNodeFileDocument(const FileSystem& fileSystem, PoolString ref);

}  // namespace BECore
