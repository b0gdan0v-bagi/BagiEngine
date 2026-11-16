#include "ArtGenLaunch.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_process.h>
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
        return String("python");
#else
        return String("python3");
#endif
    }

    Vector<String> BuildBridgeLaunch(StringView python, StringView script, uint16_t port) {
        Vector<String> arguments;
        arguments.push_back(String(python));
        arguments.push_back(String(script));
        arguments.push_back(String("--port"));
        arguments.push_back(Format("{}", port));
        arguments.push_back(String("--exit-with-editor"));
        return arguments;
    }

    bool LaunchArtGenBridge(const std::filesystem::path& script, uint16_t port, String& error) {
        error.clear();
        const std::string scriptText = script.generic_string();  //-NoStd: std::filesystem gives a std::string
        const String python = ArtGenPython();
        const Vector<String> arguments = BuildBridgeLaunch(python, StringView(scriptText.data(), scriptText.size()), port);
        Vector<const char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const String& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        SDL_Process* process = SDL_CreateProcess(argv.data(), false);
        if (process == nullptr) {
            error = Format("could not start {}: {}", python, SDL_GetError());
            return false;
        }
        // Destroying the handle does not stop the child: the bridge lives until the editor goes away
        SDL_DestroyProcess(process);
        return true;
    }

}  // namespace BECore
