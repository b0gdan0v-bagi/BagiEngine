#include "CommandLine.h"

namespace BECore {

    namespace {

        constexpr char kValueSeparator = '=';

        StringView AsView(const String& text) {
            return StringView(text);
        }

    }  // namespace

    void CommandLine::Set(Span<const String> arguments) {
        _arguments.assign(arguments.begin(), arguments.end());
    }

    Optional<StringView> CommandLine::FindValue(StringView name) const {
        for (const String& argument : _arguments) {
            const StringView view = AsView(argument);
            if (view.size() > name.size() && view.starts_with(name) && view[name.size()] == kValueSeparator) {
                return view.substr(name.size() + 1);
            }
        }
        return std::nullopt;
    }

    bool CommandLine::HasFlag(StringView name) const {
        for (const String& argument : _arguments) {
            if (AsView(argument) == name) {
                return true;
            }
        }
        return false;
    }

}  // namespace BECore
