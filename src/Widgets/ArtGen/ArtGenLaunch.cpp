#include "ArtGenLaunch.h"

#include <BECore/File/File.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

namespace BECore {

    Optional<Path> FindArtGenBridge(const Vector<Path>& configLayers) {
        for (const Path& layer : configLayers) {
            Path script = layer.Parent() / kArtGenBridgeScript;
            if (const auto info = GetFileInfo(script); info && info->kind == FileKind::File) {
                return script;
            }
        }
        return std::nullopt;
    }

    String ArtGenPython() {
        if (const char* configured = SDL_getenv("BAGI_PYTHON"); configured != nullptr && configured[0] != '\0') {
            return String(configured);
        }
#if defined(_WIN32)
        return String("pythonw");
#else
        return String("python3");
#endif
    }

    Vector<String> BuildBridgeLaunch(StringView python, StringView script, uint16_t port, StringView log) {
        Vector<String> arguments;
        arguments.push_back(String(python));
        arguments.push_back(String(script));
        arguments.push_back(String("--port"));
        arguments.push_back(Format("{}", port));
        arguments.push_back(String("--exit-with-editor"));
        arguments.push_back(String("--log"));
        arguments.push_back(String(log));
        return arguments;
    }

    bool LaunchArtGenBridge(const Path& script, uint16_t port, const Path& log, String& error) {
        error.clear();
        const String python = ArtGenPython();
        const Vector<String> arguments = BuildBridgeLaunch(python, script.View(), port, log.View());
        Vector<const char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const String& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
        SDL_Process* process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (process == nullptr) {
            error = Format("could not start {}: {}", python, SDL_GetError());
            return false;
        }
        // Destroying the handle does not stop the child: the bridge lives until the editor goes away
        SDL_DestroyProcess(process);
        return true;
    }

}  // namespace BECore
