#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /// One argument found by name: `--name` (no value) or `--name=value` (`hasValue`, the value may be empty). The view points into the CommandLine
    struct CommandLineArgument {
        StringView value;
        bool hasValue = false;
    };

    /**
     * @brief The arguments of the program (without the program's name), readable by a game or a test.
     *
     * `Application::SetArguments` stores them in `CoreManager::GetCommandLine()`; a game reads its own flags there
     * and never asks `Application`, so it can be built and tested without the runtime module. A flag the engine
     * does not know is ignored by the engine's own parsers, so a game's flags need nothing else. The engine's readers
     * (ResolveControlSettings among them) all go through this class, so a repeated flag means one thing everywhere.
     */
    class CommandLine {
    public:
        void Set(Span<const String> arguments);

        /// The path the program was started by (`argv[0]`) as it was given, so it may be relative to the working folder or a bare name; it is not
        /// one of GetArguments(). Empty when nobody set it
        void SetProgramPath(StringView path) {
            _programPath.assign(path.data(), path.size());
        }

        [[nodiscard]] StringView GetProgramPath() const {
            return _programPath;
        }

        Span<const String> GetArguments() const {
            return _arguments;
        }

        /// The value of the first `--name=value` (the part after the first `=`, empty allowed); nullopt when there is none.
        /// `--name` alone has no value, and `--namesake=1` is not `--name`. The view is valid until the next Set()
        Optional<StringView> FindValue(StringView name) const;

        /// The first argument that is `--name` or `--name=value`, in either form: a bare flag and a value form of one name are one flag,
        /// and the first of them wins, as for a repeated flag. `--namesake=1` is not `--name`. The view is valid until the next Set()
        Optional<CommandLineArgument> Find(StringView name) const;

        /// Whether the bare `--name` is among the arguments (`--name=value` is not a flag)
        bool HasFlag(StringView name) const;

    private:
        Vector<String> _arguments;
        String _programPath;
    };

}  // namespace BECore
