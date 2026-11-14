#pragma once

namespace BECore {

    /**
     * @brief The arguments of the program (without the program's name), readable by a game or a test.
     *
     * `Application::SetArguments` stores them in `CoreManager::GetCommandLine()`; a game reads its own flags there
     * and never asks `Application`, so it can be built and tested without the runtime module. A flag the engine
     * does not know is ignored by the engine's own parsers, so a game's flags need nothing else.
     */
    class CommandLine {
    public:
        /// Replaces the arguments
        void Set(Span<const String> arguments);

        Span<const String> GetArguments() const {
            return _arguments;
        }

        /// The value of the first `--name=value` (the part after the first `=`, empty allowed); nullopt when there is none.
        /// `--name` alone has no value, and `--namesake=1` is not `--name`. The view is valid until the next Set()
        Optional<StringView> FindValue(StringView name) const;

        /// Whether the bare `--name` is among the arguments (`--name=value` is not a flag)
        bool HasFlag(StringView name) const;

    private:
        Vector<String> _arguments;
    };

}  // namespace BECore
