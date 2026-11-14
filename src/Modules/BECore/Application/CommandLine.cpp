#include "CommandLine.h"

namespace BECore {

    namespace {

        constexpr char kValueSeparator = '=';

        eastl::string_view AsView(const eastl::string& text) {
            return eastl::string_view(text.data(), text.size());
        }

    }  // namespace

    void CommandLine::Set(eastl::span<const eastl::string> arguments) {
        _arguments.assign(arguments.begin(), arguments.end());
    }

    Optional<eastl::string_view> CommandLine::FindValue(eastl::string_view name) const {
        for (const eastl::string& argument : _arguments) {
            const eastl::string_view view = AsView(argument);
            if (view.size() > name.size() && view.starts_with(name) && view[name.size()] == kValueSeparator) {
                return view.substr(name.size() + 1);
            }
        }
        return std::nullopt;
    }

    bool CommandLine::HasFlag(eastl::string_view name) const {
        for (const eastl::string& argument : _arguments) {
            if (AsView(argument) == name) {
                return true;
            }
        }
        return false;
    }

}  // namespace BECore
