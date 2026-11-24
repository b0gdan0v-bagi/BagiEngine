#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// The bridge script, relative to the engine checkout
    inline constexpr StringView kArtGenBridgeScript = "tools/artgen/artgen_bridge.py";

    /**
     * @brief The generation service's bridge among the content roots: `<root>/tools/artgen/artgen_bridge.py` of the first root (the parent of a
     * `config` layer) that has it, which is the engine checkout. Nullopt when no root has it (a packaged build, the web).
     */
    [[nodiscard]] Optional<Path> FindArtGenBridge(const Vector<Path>& configLayers);

    /// The Python to run the bridge with: `$BAGI_PYTHON` when set, else `pythonw` on Windows (no console window over the editor) and `python3`
    /// elsewhere, looked up on PATH
    [[nodiscard]] String ArtGenPython();

    /// The arguments that start the bridge for the editor on @p port, writing its lines to @p log; it exits when that editor goes away
    [[nodiscard]] Vector<String> BuildBridgeLaunch(StringView python, StringView script, uint16_t port, StringView log);

    /**
     * @brief Starts the bridge in the background (no standard streams: an editor started from the desktop has none to hand over), not waited
     * for; its lines go to @p log. False with @p error (from SDL) when the process could not be created.
     */
    bool LaunchArtGenBridge(const Path& script, uint16_t port, const Path& log, String& error);

}  // namespace BECore
