#pragma once

#include <BECore/File/Path.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief A tool script of the engine, found among the content roots: `<root>/<script>` of the first root (the parent of a `config` layer) that has it
     * as a file, which is the engine checkout. Nullopt when no root has it (a packaged build, the web).
     */
    [[nodiscard]] Optional<Path> FindEngineScript(const Vector<Path>& configLayers, StringView script);

    /// The Python to run a tool script with: `$BAGI_PYTHON` when set, else `pythonw` on Windows (no console window over the editor) and `python3`
    /// elsewhere, looked up on PATH
    [[nodiscard]] String EnginePython();

}  // namespace BECore
