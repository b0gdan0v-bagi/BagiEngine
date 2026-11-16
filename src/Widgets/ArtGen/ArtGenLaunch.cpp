#include "ArtGenLaunch.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

namespace BECore {

    Optional<std::filesystem::path> FindArtGenBridge(const Vector<std::filesystem::path>& configLayers) {
        for (const auto& layer : configLayers) {
            std::filesystem::path script = layer.parent_path() / ToStd(kArtGenBridgeScript);  //-NoStd: std::filesystem::path takes a std::string
            std::error_code error;
            if (std::filesystem::is_regular_file(script, error)) {
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

    bool LaunchArtGenBridge(const std::filesystem::path& script, uint16_t port, const std::filesystem::path& log, String& error) {
        error.clear();
        const std::string scriptText = script.generic_string();  //-NoStd: std::filesystem gives a std::string
        const std::string logText = log.generic_string();        //-NoStd: std::filesystem gives a std::string
        const String python = ArtGenPython();
        const Vector<String> arguments = BuildBridgeLaunch(python, StringView(scriptText.data(), scriptText.size()), port, StringView(logText.data(), logText.size()));
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
