#include "ControlSettings.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/CommandLine.h>

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

        // Through CommandLine, as every reader of the arguments: a flag given twice takes its first value everywhere (Control/CLAUDE.md)
        CommandLine commandLine;
        commandLine.Set(args);
        // A bare `--name` of a flag that takes a value is an error that shows the form, not an unknown argument
        const auto bareFlag = [&commandLine](StringView prefix, StringView what, StringView form) -> Optional<ControlError> {
            if (commandLine.HasFlag(FlagName(prefix))) {
                return InvalidSetting(Format("'{}' needs {}: {}{}", FlagName(prefix), what, prefix, form));
            }
            return std::nullopt;
        };

        settings.enabled = settings.enabled || commandLine.HasFlag(ControlArguments::kControl);
        settings.manualClock = settings.manualClock || commandLine.HasFlag(ControlArguments::kManualClock);
        settings.headless = settings.headless || commandLine.HasFlag(ControlArguments::kHeadless);
        settings.replayExit = commandLine.HasFlag(ControlArguments::kReplayExit);

        const Optional<StringView> portValue = commandLine.FindValue(FlagName(ControlArguments::kControlPortPrefix));
        const bool portOnCommandLine = portValue.has_value();
        if (portValue) {
            const auto port = ParsePort(*portValue);
            if (!port) {
                return std::unexpected(InvalidSetting(Format("'{}{}' is not a port (0 to 65535)", ControlArguments::kControlPortPrefix, *portValue)));
            }
            settings.port = *port;
        } else if (auto error = bareFlag(ControlArguments::kControlPortPrefix, "a value", "N")) {
            return std::unexpected(std::move(*error));
        }

        if (const Optional<StringView> replay = commandLine.FindValue(FlagName(ControlArguments::kReplayPrefix))) {
            if (replay->empty()) {
                return std::unexpected(InvalidSetting(Format("'{}' has no file: {}FILE", ControlArguments::kReplayPrefix, ControlArguments::kReplayPrefix)));
            }
            settings.replayPath = String(*replay);
        } else if (auto error = bareFlag(ControlArguments::kReplayPrefix, "a file", "FILE")) {
            return std::unexpected(std::move(*error));
        }

        if (const Optional<StringView> window = commandLine.FindValue(FlagName(ControlArguments::kWindowPrefix))) {
            settings.windowSize = ParseWindowSize(*window);
            if (!settings.windowSize) {
                return std::unexpected(InvalidSetting(Format("'{}{}' is not a window size: two integers, {}WxH (for example {}480x480)", ControlArguments::kWindowPrefix, *window,
                                                             ControlArguments::kWindowPrefix, ControlArguments::kWindowPrefix)));
            }
        } else if (auto error = bareFlag(ControlArguments::kWindowPrefix, "a value", "WxH")) {
            return std::unexpected(std::move(*error));
        }

        if (const Optional<StringView> density = commandLine.FindValue(FlagName(ControlArguments::kDensityPrefix))) {
            settings.density = StringUtils::ParseNumber<float>(*density);
            if (!settings.density) {
                return std::unexpected(InvalidSetting(Format("'{}{}' is not a number: {}D", ControlArguments::kDensityPrefix, *density, ControlArguments::kDensityPrefix)));
            }
        } else if (auto error = bareFlag(ControlArguments::kDensityPrefix, "a value", "D")) {
            return std::unexpected(std::move(*error));
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
