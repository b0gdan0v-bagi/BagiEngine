#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>
#include <filesystem>

namespace BECore {

    /// The bridge script, relative to the engine checkout
    inline constexpr StringView kArtGenBridgeScript = "tools/artgen/artgen_bridge.py";

    /**
     * @brief The generation service's bridge among the content roots: `<root>/tools/artgen/artgen_bridge.py` of the first root (the parent of a
     * `config` layer) that has it, which is the engine checkout. Nullopt when no root has it (a packaged build, the web).
     */
    [[nodiscard]] Optional<std::filesystem::path> FindArtGenBridge(const Vector<std::filesystem::path>& configLayers);

    /// The Python to run the bridge with: `$BAGI_PYTHON` when set, else `python` on Windows and `python3` elsewhere (looked up on PATH)
    [[nodiscard]] String ArtGenPython();

    /// The arguments that start the bridge for the editor on @p port; it exits when that editor goes away (`--exit-with-editor`)
    [[nodiscard]] Vector<String> BuildBridgeLaunch(StringView python, StringView script, uint16_t port);

    /// Starts the bridge as a process of its own, not waited for; false with @p error (from SDL) when it could not be started
    bool LaunchArtGenBridge(const std::filesystem::path& script, uint16_t port, String& error);

}  // namespace BECore
