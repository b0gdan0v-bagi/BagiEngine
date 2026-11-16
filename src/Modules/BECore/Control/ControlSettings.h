#pragma once

#include <BECore/Control/ControlTypes.h>
#include <cstdint>
#include <expected>

namespace BECore {

    inline constexpr uint16_t kDefaultControlPort = 8765;

    /// Flags of the command line that Application hands to ResolveControlSettings()
    namespace ControlArguments {
        inline constexpr StringView kControl = "--control";
        inline constexpr StringView kControlPortPrefix = "--control-port=";
        inline constexpr StringView kManualClock = "--manual-clock";
        inline constexpr StringView kHeadless = "--headless";
        inline constexpr StringView kReplayPrefix = "--replay=";
        inline constexpr StringView kReplayExit = "--replay-exit";
    }  // namespace ControlArguments

    struct ApplicationConfigLoad;  // Application/ApplicationConfig.h, which includes this file

    /// How the external control API is started: off by default, a development tool that has to be asked for
    struct ControlSettings {
        bool enabled = false;
        uint16_t port = kDefaultControlPort;  // 0 asks the system for a free port
        bool manualClock = false;             // frames run only on a `step`; ControlService::SetManualClock
        bool headless = false;                // no window: the SDL software renderer on the dummy video driver (ApplicationOptions)
        String replayPath;                    // --replay=FILE: Application plays this replay file once the game is up; empty is none
        bool replayExit = false;              // --replay-exit: the application quits when the replay is over
    };

    /**
     * @brief Merges the command line, the ApplicationConfig fields and the defaults into the control settings.
     *
     * Priority: command line, then attribute, then default. The flags, the attributes and the errors of the file half:
     * Control/CLAUDE.md, "Starting the API".
     *
     * Arguments it does not know are ignored (a game may have flags of its own). A value that cannot be read, or a flag
     * that needs the API while it stays off, is an error: the caller stops the start-up, because a person who asked for
     * the API has to be told that it did not start. Whether headless mode fits the run mode is for the caller to check
     * (it knows the mode).
     *
     * @param args Command line without the program name
     * @param file The loaded ApplicationConfig; a default one (no config, `found == false`) is allowed. Errors of other fields (`mode`,
     *             `type`) are not this function's business
     */
    [[nodiscard]] std::expected<ControlSettings, ControlError> ResolveControlSettings(Span<const String> args, const ApplicationConfigLoad& file);

}  // namespace BECore
