#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/MainWindow/WindowSimulation.h>
#include <BECore/Reflection/IDeserializer.h>
#include <cstdint>
#include <expected>

namespace BECore {

    inline constexpr uint16_t kDefaultControlPort = 8765;

    /// The flags of the command line that ResolveControlSettings() reads, as they are spelled; the reader derives each from the name of its
    /// field (CommandLineDeserializer), these only name them in the messages of the rules between settings
    namespace ControlArguments {
        inline constexpr StringView kControl = "--control";
        inline constexpr StringView kControlPort = "--control-port";
        inline constexpr StringView kManualClock = "--manual-clock";
        inline constexpr StringView kHeadless = "--headless";
        inline constexpr StringView kReplay = "--replay";
        inline constexpr StringView kReplayExit = "--replay-exit";
        inline constexpr StringView kWindow = "--window";
        inline constexpr StringView kDensity = "--density";
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
        Optional<WindowSize> windowSize;      // --window=WxH: the start size of a headless window, in window units; inside WindowLimits
        Optional<float> density;              // --density=D: its output pixels per window unit; inside WindowLimits. Both flags need headless
        /// Findings of the command line that are no error: an argument that looks like a mistyped flag of these settings
        /// (`--contol-port`). The caller logs them; they never stop a start-up
        Vector<DeserializeError> warnings;
    };

    /**
     * @brief Merges the command line, the ApplicationConfig fields and the defaults into the control settings.
     *
     * Priority: command line, then attribute, then default, field by field: one CommandLineDeserializer reads the flags over a copy of
     * the file's config, so a flag the command line lacks leaves the file's value. A boolean flag may be switched off the same way
     * (`--control=false`). The flags, the attributes and the errors of the file half: `.claude/rules/control-startup.md`.
     *
     * An argument it does not know is ignored (a game may have flags of its own); one that is near to a flag of these settings is a
     * warning in `warnings`. A value that cannot be read, a flag that needs the API while it stays off, and the other rules between the
     * settings are errors: the caller stops the start-up, because a person who asked for the API has to be told that it did not start.
     * Whether headless mode fits the run mode is for the caller to check (it knows the mode).
     *
     * @param args Command line without the program name
     * @param file The loaded ApplicationConfig; a default one (no config, `found == false`) is allowed. Errors of other fields (`mode`,
     *             `type`) are not this function's business
     */
    [[nodiscard]] std::expected<ControlSettings, ControlError> ResolveControlSettings(Span<const String> args, const ApplicationConfigLoad& file);

}  // namespace BECore
