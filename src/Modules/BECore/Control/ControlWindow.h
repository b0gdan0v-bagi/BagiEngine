#pragma once

#include <BECore/Control/JsonWriter.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// Error codes of the window methods. Part of the protocol: clients match on them
    namespace WindowControlErrors {
        /// `set_window` on a window that is not headless, or with no window at all: it changes nothing
        inline constexpr StringView kNotHeadless = "not_headless";
    }  // namespace WindowControlErrors

    /**
     * @brief Writes the window as `status` and `set_window` answer it, read from the main window, the renderer and the UI environment.
     *
     * One object: {"width","height" (window units), "density", "displayScale", "safeArea":{"top","right","bottom","left"} (window units), "outputWidth","outputHeight" (pixels, 0 without a
     * renderer), "userScale", "headless"}, written with its keys in alphabetical order. Without a window the size and the insets are 0, the density and the scales 1 and "headless" false.
     * Main thread. @p writer is positioned where one value is due (the `window` member of `status`, the result of a call).
     */
    void WriteWindowStatus(JsonWriter& writer);

    /**
     * @brief Registers `set_window`: {"width","height" (required), "density"?, "userScale"?, "safeArea"?:{"top","right","bottom","left"}}.
     *
     * Headless windows only (`not_headless` otherwise). A value that is left out keeps its current one. Everything is checked after the merge
     * (WindowLimits, UISettings::kMinUserScale), and nothing is applied unless all of it is valid. The answer comes after one frame and is WriteWindowStatus(), so
     * the layout and a capture that follow are of the new size. Details: `.claude/rules/control-window-api.md`.
     */
    void RegisterWindowControl(ControlService& service);

}  // namespace BECore
