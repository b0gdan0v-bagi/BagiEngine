#include "ControlSettings.h"

namespace BECore {

    namespace {

        /// The whole of @p text as a port; nullopt for an empty text, a sign, a trailing character or a number above 65535
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

    std::expected<ControlSettings, ControlError> ResolveControlSettings(eastl::span<const eastl::string> args, const XmlNode& applicationConfig) {
        ControlSettings settings;

        // Attributes first, so that the command line overrides them
        if (const auto text = applicationConfig.GetAttribute(ControlAttributes::kControl)) {
            const auto enabled = applicationConfig.ParseAttribute<bool>(ControlAttributes::kControl);
            if (!enabled) {
                return std::unexpected(InvalidSetting(Format("ApplicationConfig: {}=\"{}\" is not a boolean (true, false, 1 or 0)", ControlAttributes::kControl, *text)));
            }
            settings.enabled = *enabled;
        }
        if (const auto text = applicationConfig.GetAttribute(ControlAttributes::kControlPort)) {
            const auto port = ParsePort(*text);
            if (!port) {
                return std::unexpected(InvalidSetting(Format("ApplicationConfig: {}=\"{}\" is not a port (0 to 65535)", ControlAttributes::kControlPort, *text)));
            }
            settings.port = *port;
        }
        if (const auto text = applicationConfig.GetAttribute(ControlAttributes::kManualClock)) {
            const auto manualClock = applicationConfig.ParseAttribute<bool>(ControlAttributes::kManualClock);
            if (!manualClock) {
                return std::unexpected(InvalidSetting(Format("ApplicationConfig: {}=\"{}\" is not a boolean (true, false, 1 or 0)", ControlAttributes::kManualClock, *text)));
            }
            settings.manualClock = *manualClock;
        }

        bool portOnCommandLine = false;
        for (const eastl::string& arg : args) {
            const eastl::string_view argument(arg.data(), arg.size());
            if (argument == ControlArguments::kControl) {
                settings.enabled = true;
            } else if (argument == ControlArguments::kManualClock) {
                settings.manualClock = true;
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
        return settings;
    }

}  // namespace BECore
