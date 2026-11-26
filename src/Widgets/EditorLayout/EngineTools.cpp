#include "EngineTools.h"

#include <BECore/File/File.h>
#include <SDL3/SDL_stdinc.h>

namespace BECore {

    Optional<Path> FindEngineScript(const Vector<Path>& configLayers, StringView script) {
        for (const Path& layer : configLayers) {
            Path found = layer.Parent() / script;
            if (const auto info = GetFileInfo(found); info && info->kind == FileKind::File) {
                return found;
            }
        }
        return std::nullopt;
    }

    String EnginePython() {
        if (const char* configured = SDL_getenv("BAGI_PYTHON"); configured != nullptr && configured[0] != '\0') {
            return String(configured);
        }
#if defined(_WIN32)
        return String("pythonw");
#else
        return String("python3");
#endif
    }

}  // namespace BECore
