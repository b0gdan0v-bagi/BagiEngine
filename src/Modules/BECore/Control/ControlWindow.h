#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// Error codes of the window methods. Part of the protocol: clients match on them
    namespace WindowControlErrors {
        /// `set_window` on a window that is not headless, or with no window at all: it changes nothing
        inline constexpr StringView kNotHeadless = "not_headless";
    }  // namespace WindowControlErrors

    /**
     * @brief Registers `set_window`: {"width","height" (required), "density"?, "userScale"?, "safeArea"?:{"top","right","bottom","left"}}.
     *
     * Headless windows only (`not_headless` otherwise). A value that is left out keeps its current one. Everything is checked after the merge
     * (WindowLimits, UISettings::kMinUserScale), and nothing is applied unless all of it is valid. The answer comes after one frame and is ControlWindowStatus::Read(), so
     * the layout and a capture that follow are of the new size. Details: `.claude/rules/control-window-api.md`.
     */
    void RegisterWindowControl(ControlService& service);

}  // namespace BECore
