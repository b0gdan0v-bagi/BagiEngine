#pragma once

#include <BECore/Control/ControlTypes.h>

namespace BECore {

    /// Port of the control API when nothing else is asked for
    inline constexpr uint16_t kDefaultControlPort = 8765;

    /// Flags of the command line that Application hands to ResolveControlSettings()
    namespace ControlArguments {
        inline constexpr eastl::string_view kControl = "--control";
        inline constexpr eastl::string_view kControlPortPrefix = "--control-port=";
        inline constexpr eastl::string_view kManualClock = "--manual-clock";
        inline constexpr eastl::string_view kHeadless = "--headless";
    }  // namespace ControlArguments

    /// Attributes of the ApplicationConfig root that ResolveControlSettings() reads
    namespace ControlAttributes {
        inline constexpr eastl::string_view kControl = "control";
        inline constexpr eastl::string_view kControlPort = "controlPort";
        inline constexpr eastl::string_view kManualClock = "manualClock";
        inline constexpr eastl::string_view kHeadless = "headless";
    }  // namespace ControlAttributes

    /// How the external control API is started: off by default, a development tool that has to be asked for
    struct ControlSettings {
        bool enabled = false;
        uint16_t port = kDefaultControlPort;  // 0 asks the system for a free port
        bool manualClock = false;             // frames run only on a `step`; ControlService::SetManualClock
        bool headless = false;                // no window: the SDL software renderer on the dummy video driver (ApplicationOptions)
    };

    /**
     * @brief Merges the command line, the ApplicationConfig attributes and the defaults into the control settings.
     *
     * Priority: command line, then attribute, then default (off, port kDefaultControlPort).
     *
     * | Command line | Attribute | Meaning |
     * |--------------|-----------|---------|
     * | --control | control="true" / "false" (also 1 / 0) | start the WebSocket server |
     * | --control-port=N | controlPort="N" | port, 0 to 65535 (0 is a free port picked by the system) |
     * | --manual-clock | manualClock="true" / "false" (also 1 / 0) | the game clock moves only on a `step` (see ControlService) |
     * | --headless | headless="true" / "false" (also 1 / 0) | no window, drawing in memory; a screenshot is the way to see the game |
     *
     * Arguments it does not know are ignored (a game may have flags of its own). A value that cannot be read, a bare
     * `--control-port`, and `--control-port`, the manual clock or headless mode while the API stays off are errors: the
     * caller stops the start-up, because a person who asked for the API has to be told that it did not start (and a
     * manual clock that nobody can step would freeze the game, and a game without a window that nobody can reach
     * would run unseen). Whether headless mode fits the run mode is for the caller to check (it knows the mode).
     *
     * @param args Command line without the program name
     * @param applicationConfig Root of ApplicationConfig; an empty node is allowed
     */
    [[nodiscard]] std::expected<ControlSettings, ControlError> ResolveControlSettings(eastl::span<const eastl::string> args, const XmlNode& applicationConfig);

}  // namespace BECore
