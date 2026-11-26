#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /**
     * @brief The scenes an export can be asked to leave out, by name and in file order. No ImGui.
     *
     * They are the child elements of `<scenes>` of the `SceneConfig.xml` of the highest of @p configLayers (lowest first) that has one: the file the
     * game reads and the one the export script cuts (`tools/export/CLAUDE.md`, "Scenes left out"). An error sentence when there is none, when that
     * file is only cooked or when it cannot be read: nothing can be left out of such a game.
     */
    [[nodiscard]] std::expected<Vector<String>, String> ReadExportScenes(Span<const Path> configLayers);

}  // namespace BECore
