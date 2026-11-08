#include "ControlSettings.h"

#include <BECore/Application/ApplicationConfig.h>

namespace BECore {

    namespace {

        /// The paths of the ApplicationConfig fields that are control settings (the names of the attributes)
        bool IsControlField(eastl::string_view path) {
            using namespace ApplicationConfigFields;
            return path == kControl || path == kControlPort || path == kManualClock || path == kHeadless;
        }

        /// The whole of @p text (the value of --control-port=N) as a port; nullopt for an empty text, a sign, a trailing character or a number above 65535
        std::optional<uint16_t> ParsePort(eastl::string_view text) {
            uint16_t port = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return port;
        }

        ControlError InvalidSetting(eastl::string message) {
            return MakeControlError(ControlErrors::kInvalidParams, std::move(message));
        }

    }  // namespace

    std::expected<ControlSettings, ControlError> ResolveControlSettings(eastl::span<const eastl::string> args, const ApplicationConfigLoad& file) {
        // The file first, so that the command line overrides it. A bad value is an error even when the API is off or a flag would override it
        for (const DeserializeError& error : file.errors) {
            if (IsControlField(error.path.ToStringView())) {
                return std::unexpected(InvalidSetting(Format("ApplicationConfig: {}: {}", error.path.ToStringView(), error.errorMessage.ToStringView())));
            }
        }

        ControlSettings settings;
        settings.enabled = file.config._control;
        settings.port = file.config._controlPort;
        settings.manualClock = file.config._manualClock;
        settings.headless = file.config._headless;

        // The flags are parsed by hand; a later stage replaces this loop with a name to value source
        bool portOnCommandLine = false;
        for (const eastl::string& arg : args) {
            const eastl::string_view argument(arg.data(), arg.size());
            if (argument == ControlArguments::kControl) {
                settings.enabled = true;
            } else if (argument == ControlArguments::kManualClock) {
                settings.manualClock = true;
            } else if (argument == ControlArguments::kHeadless) {
                settings.headless = true;
            } else if (argument.starts_with(ControlArguments::kControlPortPrefix)) {
                const eastl::string_view value = argument.substr(ControlArguments::kControlPortPrefix.size());
                const auto port = ParsePort(value);
                if (!port) {
                    return std::unexpected(InvalidSetting(Format("'{}' is not a port (0 to 65535)", argument)));
                }
                settings.port = *port;
                portOnCommandLine = true;
            } else if (argument == ControlArguments::kControlPortPrefix.substr(0, ControlArguments::kControlPortPrefix.size() - 1)) {
                return std::unexpected(InvalidSetting(Format("'{}' needs a value: {}N", argument, ControlArguments::kControlPortPrefix)));
            }
        }

        if (portOnCommandLine && !settings.enabled) {
            return std::unexpected(InvalidSetting(Format("{}N has no effect without {}", ControlArguments::kControlPortPrefix, ControlArguments::kControl)));
        }
        if (settings.manualClock && !settings.enabled) {
            return std::unexpected(InvalidSetting(Format("{} needs {}: without the control API nobody could step the game", ControlArguments::kManualClock, ControlArguments::kControl)));
        }
        if (settings.headless && !settings.enabled) {
            return std::unexpected(
                InvalidSetting(Format("{} needs {}: without the control API nobody could reach a game that has no window", ControlArguments::kHeadless, ControlArguments::kControl)));
        }
        return settings;
    }

}  // namespace BECore
