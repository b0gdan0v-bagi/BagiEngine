#include "ControlSettings.h"

#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Application/CommandLine.h>
#include <BECore/Application/CommandLineDeserializer.h>
#include <BECore/Control/ControlFlags.h>

namespace BECore {

    namespace {

        /// The only keys the command line may fill: the four control fields of ApplicationConfig and the flag-only ControlFlags. The other
        /// fields (`type`, `renderer`, `path`, `mode`, ...) are names a game may use for flags of its own
        constexpr StringView kFlagKeys[] = {ApplicationConfigFields::kControl,
                                            ApplicationConfigFields::kControlPort,
                                            ApplicationConfigFields::kManualClock,
                                            ApplicationConfigFields::kHeadless,
                                            ControlFlagFields::kReplay,
                                            ControlFlagFields::kReplayExit,
                                            ControlFlagFields::kWindow,
                                            ControlFlagFields::kDensity};

        /// The paths of the ApplicationConfig fields that are control settings (the names of the attributes)
        bool IsControlField(StringView path) {
            using namespace ApplicationConfigFields;
            return path == kControl || path == kControlPort || path == kManualClock || path == kHeadless;
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
                    return refuse(ControlArguments::kWindow, valid.error());
                }
            }
            if (settings.density) {
                if (const auto valid = ValidateWindowSimulation({.width = size, .height = size, .density = *settings.density}); !valid) {
                    return refuse(ControlArguments::kDensity, valid.error());
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

        // Through CommandLine, as every reader of the arguments: a flag given twice takes its first value everywhere (Control/CLAUDE.md)
        CommandLine commandLine;
        commandLine.Set(args);

        // The reader runs over a copy of the file's config, so each flag overrides one field and the others keep the file's value
        ApplicationConfig config = file.config;
        ControlFlags flags;
        CommandLineDeserializer reader(commandLine, kFlagKeys);
        reader.SetDiagnostics(DiagnosticsMode::Warn);
        config.Deserialize(reader);
        flags.Deserialize(reader);
        reader.FinishLoad();
        if (reader.HasErrors()) {
            const DeserializeError& error = reader.GetErrors().front();
            return std::unexpected(InvalidSetting(Format("{}: {}", error.path, error.errorMessage)));
        }
        // The reader gives an empty text for `--replay=`, and an empty text is also "not given": only the argument tells them apart
        if (const Optional<CommandLineArgument> replay = commandLine.Find(ControlArguments::kReplay); replay && replay->value.empty()) {
            return std::unexpected(InvalidSetting(Format("{}: has no file: {}=FILE", ControlArguments::kReplay, ControlArguments::kReplay)));
        }

        ControlSettings settings;
        settings.enabled = config._control;
        settings.port = config._controlPort;
        settings.manualClock = config._manualClock;
        settings.headless = config._headless;
        settings.replayPath = std::move(flags._replay);
        settings.replayExit = flags._replayExit;
        settings.windowSize = flags._window;
        settings.density = flags._density;
        settings.warnings = reader.GetWarnings();

        if (commandLine.Find(ControlArguments::kControlPort) && !settings.enabled) {
            return std::unexpected(InvalidSetting(Format("{}=N has no effect without {}", ControlArguments::kControlPort, ControlArguments::kControl)));
        }
        if (settings.manualClock && !settings.enabled) {
            return std::unexpected(InvalidSetting(Format("{} needs {}: without the control API nobody could step the game", ControlArguments::kManualClock, ControlArguments::kControl)));
        }
        if (settings.headless && !settings.enabled) {
            return std::unexpected(
                InvalidSetting(Format("{} needs {}: without the control API nobody could reach a game that has no window", ControlArguments::kHeadless, ControlArguments::kControl)));
        }
        if (settings.replayExit && settings.replayPath.empty()) {
            return std::unexpected(InvalidSetting(Format("{} has no effect without {}=FILE", ControlArguments::kReplayExit, ControlArguments::kReplay)));
        }
        if (!settings.replayPath.empty() && !settings.enabled) {
            return std::unexpected(InvalidSetting(Format("{}=FILE needs {}: a replay is a call of the control API", ControlArguments::kReplay, ControlArguments::kControl)));
        }
        if (!settings.replayPath.empty() && !settings.manualClock) {
            return std::unexpected(InvalidSetting(Format("{}=FILE needs {}: the replay passes the game time of the file on it", ControlArguments::kReplay, ControlArguments::kManualClock)));
        }

        if ((settings.windowSize || settings.density) && !settings.headless) {
            return std::unexpected(InvalidSetting(
                Format("{} and {} need {}: the size and the density of a real window belong to the system", ControlArguments::kWindow, ControlArguments::kDensity, ControlArguments::kHeadless)));
        }
        return CheckWindowFlags(settings);
    }

}  // namespace BECore
