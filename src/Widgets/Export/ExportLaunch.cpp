#include "ExportLaunch.h"

#include <BECore/File/File.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>

namespace BECore {

    namespace {

        /// A line of the script's standard input that gives the export up (`--cancel-on-stdin-close`)
        constexpr StringView kCancelLine = "cancel\n";
#if defined(_WIN32)
        constexpr StringView kExecutableSuffix = ".exe";
#endif

        bool IsFile(StringView path) {
            const auto info = GetFileInfo(path);
            return info && info->kind == FileKind::File;
        }

        /// The null-ended argument array SDL takes; it points into @p arguments
        Vector<const char*> ArgumentPointers(Span<const String> arguments) {
            Vector<const char*> pointers;
            pointers.reserve(arguments.size() + 1);
            for (const String& argument : arguments) {
                pointers.push_back(argument.c_str());
            }
            pointers.push_back(nullptr);
            return pointers;
        }

        class SdlExportProcess final : public IExportProcess {
        public:
            SdlExportProcess(SDL_Process* process, uint64_t killGraceMs) : _process(process), _killGraceMs(killGraceMs) {}

            ~SdlExportProcess() override {
                Cancel();
                // Destroying the handle does not stop the child: the closed input is what makes the script end its build and itself
                SDL_DestroyProcess(_process);
            }

            SdlExportProcess(const SdlExportProcess&) = delete;
            SdlExportProcess& operator=(const SdlExportProcess&) = delete;

            Optional<int> Poll() override {
                if (_exitCode) {
                    return _exitCode;
                }
                int code = 0;
                if (SDL_WaitProcess(_process, false, &code)) {
                    _exitCode = code;
                } else if (_cancelledAtMs && !_killed && SDL_GetTicks() - *_cancelledAtMs >= _killGraceMs) {
                    _killed = SDL_KillProcess(_process, true);
                }
                return _exitCode;
            }

            void Cancel() override {
                if (_exitCode || _cancelledAtMs) {
                    return;
                }
                _cancelledAtMs = SDL_GetTicks();
                if (SDL_IOStream* input = SDL_GetProcessInput(_process)) {
                    // The end of the input says the same, so a write that fails loses nothing
                    SDL_WriteIO(input, kCancelLine.data(), kCancelLine.size());
                    SDL_CloseIO(input);
                }
            }

        private:
            SDL_Process* _process = nullptr;
            uint64_t _killGraceMs = 0;
            Optional<uint64_t> _cancelledAtMs;
            Optional<int> _exitCode;
            bool _killed = false;
        };

        class SdlExportLauncher final : public IExportLauncher {
        public:
            explicit SdlExportLauncher(uint64_t killGraceMs) : _killGraceMs(killGraceMs) {}

            std::expected<UniquePtr<IExportProcess>, String> Start(Span<const String> arguments, StringView log) override {
                if (arguments.empty()) {
                    return std::unexpected(String("nothing to start"));
                }
                const String logPath(log.data(), log.size());
                // A file and not a pipe: nobody reads while the panel is closed, and a full pipe would stop the build
                SDL_IOStream* output = SDL_IOFromFile(logPath.c_str(), "wb");
                if (output == nullptr) {
                    return std::unexpected(Format("cannot write the log '{}': {}", log, SDL_GetError()));
                }
                Vector<const char*> argv = ArgumentPointers(arguments);
                const SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
                SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
                SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
                SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, output);
                SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
                // Not a background process: SDL then knows neither its exit code nor, on macOS, its pid
                SDL_Process* process = SDL_CreateProcessWithProperties(props);
                const String problem = process == nullptr ? Format("could not start {}: {}", arguments[0], SDL_GetError()) : String();
                SDL_DestroyProperties(props);
                SDL_CloseIO(output);  // the child has its own handle of the file
                if (process == nullptr) {
                    return std::unexpected(problem);
                }
                return UniquePtr<IExportProcess>(MakeUnique<SdlExportProcess>(process, _killGraceMs));
            }

            std::expected<void, String> StartDetached(Span<const String> arguments) override {
                if (arguments.empty()) {
                    return std::unexpected(String("nothing to start"));
                }
                Vector<const char*> argv = ArgumentPointers(arguments);
                const SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
                SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
                SDL_Process* process = SDL_CreateProcessWithProperties(props);
                const String problem = process == nullptr ? Format("could not start {}: {}", arguments[0], SDL_GetError()) : String();
                SDL_DestroyProperties(props);
                if (process == nullptr) {
                    return std::unexpected(problem);
                }
                SDL_DestroyProcess(process);
                return {};
            }

        private:
            uint64_t _killGraceMs = 0;
        };

        Vector<String> ScriptCommand(StringView python, StringView script, StringView command) {
            Vector<String> arguments;
            arguments.emplace_back(python.data(), python.size());
            arguments.emplace_back(script.data(), script.size());
            arguments.emplace_back(command.data(), command.size());
            return arguments;
        }

    }  // namespace

    Vector<String> BuildExportLaunch(StringView python, StringView script, const ExportRequest& request) {
        Vector<String> arguments = ScriptCommand(python, script, "build");
        arguments.emplace_back("--exe");
        arguments.emplace_back(request.executable.data(), request.executable.size());
        arguments.emplace_back("--config");
        arguments.emplace_back(request.config == ExportConfig::Release ? "release" : "debug");
        if (!request.outputDirectory.empty()) {
            arguments.emplace_back("--out");
            arguments.emplace_back(request.outputDirectory.data(), request.outputDirectory.size());
        }
        for (const String& scene : request.excludedScenes) {
            arguments.emplace_back("--exclude-scene");
            arguments.emplace_back(scene);
        }
        arguments.emplace_back("--cancel-on-stdin-close");
        return arguments;
    }

    Vector<String> BuildPackageRunLaunch(StringView python, StringView script, StringView packageDirectory) {
        Vector<String> arguments = ScriptCommand(python, script, "run");
        arguments.emplace_back("--dir");
        arguments.emplace_back(packageDirectory.data(), packageDirectory.size());
        return arguments;
    }

    std::expected<String, String> ResolveExportExecutable(StringView programPath) {
        if (programPath.empty()) {
            return std::unexpected(String("The editor does not know which file it runs from"));
        }
        auto path = AbsolutePath(programPath);
        if (!path) {
            return std::unexpected(std::move(path.error()));
        }
#if defined(_WIN32)
        // A Windows shell starts a program by its name without the suffix and hands that name over
        if (!IsFile(*path)) {
            path->append(kExecutableSuffix.data(), kExecutableSuffix.size());
        }
#endif
        if (!IsFile(*path)) {
            return std::unexpected(Format("There is no file '{}' to export: start the editor by its path", *path));
        }
        return std::move(*path);
    }

    UniquePtr<IExportLauncher> MakeExportLauncher(uint64_t killGraceMs) {
        return MakeUnique<SdlExportLauncher>(killGraceMs);
    }

}  // namespace BECore
