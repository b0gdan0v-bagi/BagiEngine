#include "ControlSettings.h"

#include <BECore/Application/ApplicationConfig.h>

namespace BECore {

    namespace {

        /// The paths of the ApplicationConfig fields that are control settings (the names of the attributes)
        bool IsControlField(StringView path) {
            using namespace ApplicationConfigFields;
            return path == kControl || path == kControlPort || path == kManualClock || path == kHeadless;
        }

        /// The whole of @p text (the value of --control-port=N) as a port; nullopt for an empty text, a sign, a trailing character or a number above 65535
        Optional<uint16_t> ParsePort(StringView text) {
            uint16_t port = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return port;
        }

        /// The whole of @p text (the value of --window=WxH) as two integers; nullopt for anything else. The limits are checked by the caller
        Optional<WindowSize> ParseWindowSize(StringView text) {
            const size_t separator = text.find('x');
            if (separator == StringView::npos) {
                return std::nullopt;
            }
            const auto width = StringUtils::ParseNumber<int>(text.substr(0, separator));
            const auto height = StringUtils::ParseNumber<int>(text.substr(separator + 1));
            if (!width || !height) {
                return std::nullopt;
            }
            return WindowSize{*width, *height};
        }

        /// The flag without its `=`: what a bare `--window` is told
        StringView FlagName(StringView prefix) {
            return prefix.substr(0, prefix.size() - 1);
        }

        ControlError InvalidSetting(String message) {
            return MakeControlError(ControlErrors::kInvalidParams, std::move(message));
        }

        /// Each flag against its own limits first, so the message names the flag at fault; then the two together (the output side is the size times the density)
        std::expected<ControlSettings, ControlError> CheckWindowFlags(const ControlSettings& settings) {
            const auto refuse = [](StringView what, const String& reason) { return std::unexpected(InvalidSetting(Format("{}: {}", what, reason))); };
            const int size = WindowLimits::kMinSize;
            if (settings.windowSize) {
                if (const auto valid = ValidateWindowSimulation({.width = settings.windowSize->width, .height = settings.windowSize->height}); !valid) {
                    return refuse(ControlArguments::kWindowPrefix, valid.error());
                }
            }
            if (settings.density) {
                if (const auto valid = ValidateWindowSimulation({.width = size, .height = size, .density = *settings.density}); !valid) {
                    return refuse(ControlArguments::kDensityPrefix, valid.error());
                }
            }
            if (settings.windowSize && settings.density) {
                if (const auto valid = ValidateWindowSimulation({.width = settings.windowSize->width, .height = settings.windowSize->height, .density = *settings.density}); !valid) {
                    return refuse("--window with --density", valid.error());
                }
            }
            return settings;
        }

    }  // namespace

    std::expected<ControlSettings, ControlError> ResolveControlSettings(Span<const String> args, const ApplicationConfigLoad& file) {
        // The file first, so that the command line overrides it. A bad value is an error even when the API is off or a flag would override it
        for (const DeserializeError& error : file.errors) {
            if (IsControlField(error.path)) {
                return std::unexpected(InvalidSetting(Format("ApplicationConfig: {}: {}", error.path, error.errorMessage)));
            }
        }

        ControlSettings settings;
        settings.enabled = file.config._control;
        settings.port = file.config._controlPort;
        settings.manualClock = file.config._manualClock;
        settings.headless = file.config._headless;

        bool portOnCommandLine = false;
        for (const String& arg : args) {
            const StringView argument(arg.data(), arg.size());
            if (argument == ControlArguments::kControl) {
                settings.enabled = true;
            } else if (argument == ControlArguments::kManualClock) {
                settings.manualClock = true;
            } else if (argument == ControlArguments::kHeadless) {
                settings.headless = true;
            } else if (argument.starts_with(ControlArguments::kControlPortPrefix)) {
                const StringView value = argument.substr(ControlArguments::kControlPortPrefix.size());
                const auto port = ParsePort(value);
                if (!port) {
                    return std::unexpected(InvalidSetting(Format("'{}' is not a port (0 to 65535)", argument)));
                }
                settings.port = *port;
                portOnCommandLine = true;
            } else if (argument == FlagName(ControlArguments::kControlPortPrefix)) {
                return std::unexpected(InvalidSetting(Format("'{}' needs a value: {}N", argument, ControlArguments::kControlPortPrefix)));
            } else if (argument.starts_with(ControlArguments::kReplayPrefix)) {
                const StringView value = argument.substr(ControlArguments::kReplayPrefix.size());
                if (value.empty()) {
                    return std::unexpected(InvalidSetting(Format("'{}' has no file: {}FILE", argument, ControlArguments::kReplayPrefix)));
                }
                settings.replayPath = String(value);
            } else if (argument == FlagName(ControlArguments::kReplayPrefix)) {
                return std::unexpected(InvalidSetting(Format("'{}' needs a file: {}FILE", argument, ControlArguments::kReplayPrefix)));
            } else if (argument == ControlArguments::kReplayExit) {
                settings.replayExit = true;
            } else if (argument.starts_with(ControlArguments::kWindowPrefix)) {
                settings.windowSize = ParseWindowSize(argument.substr(ControlArguments::kWindowPrefix.size()));
                if (!settings.windowSize) {
                    return std::unexpected(
                        InvalidSetting(Format("'{}' is not a window size: two integers, {}WxH (for example {}480x480)", argument, ControlArguments::kWindowPrefix, ControlArguments::kWindowPrefix)));
                }
            } else if (argument == FlagName(ControlArguments::kWindowPrefix)) {
                return std::unexpected(InvalidSetting(Format("'{}' needs a value: {}WxH", argument, ControlArguments::kWindowPrefix)));
            } else if (argument.starts_with(ControlArguments::kDensityPrefix)) {
                settings.density = StringUtils::ParseNumber<float>(argument.substr(ControlArguments::kDensityPrefix.size()));
                if (!settings.density) {
                    return std::unexpected(InvalidSetting(Format("'{}' is not a number: {}D", argument, ControlArguments::kDensityPrefix)));
                }
            } else if (argument == FlagName(ControlArguments::kDensityPrefix)) {
                return std::unexpected(InvalidSetting(Format("'{}' needs a value: {}D", argument, ControlArguments::kDensityPrefix)));
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
        if (settings.replayExit && settings.replayPath.empty()) {
            return std::unexpected(InvalidSetting(Format("{} has no effect without {}FILE", ControlArguments::kReplayExit, ControlArguments::kReplayPrefix)));
        }
        if (!settings.replayPath.empty() && !settings.enabled) {
            return std::unexpected(InvalidSetting(Format("{}FILE needs {}: a replay is a call of the control API", ControlArguments::kReplayPrefix, ControlArguments::kControl)));
        }
        if (!settings.replayPath.empty() && !settings.manualClock) {
            return std::unexpected(InvalidSetting(Format("{}FILE needs {}: the replay passes the game time of the file on it", ControlArguments::kReplayPrefix, ControlArguments::kManualClock)));
        }

        if ((settings.windowSize || settings.density) && !settings.headless) {
            return std::unexpected(InvalidSetting(Format("{} and {} need {}: the size and the density of a real window belong to the system", ControlArguments::kWindowPrefix,
                                                         ControlArguments::kDensityPrefix, ControlArguments::kHeadless)));
        }
        return CheckWindowFlags(settings);
    }

}  // namespace BECore
