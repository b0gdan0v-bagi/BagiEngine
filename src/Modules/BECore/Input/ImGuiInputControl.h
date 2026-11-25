#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class ControlService;

    /// Error codes of `imgui_input` and `imgui_find` (the protocol part that is not shared with other methods; the common ones are in ControlErrors)
    namespace ImGuiControlErrors {
        /// There is no ImGui context: the Game mode, or a game with no `ImGuiWidget`
        inline constexpr StringView kNoImGui = "no_imgui";
        inline constexpr StringView kTimeout = "imgui_timeout";
    }  // namespace ImGuiControlErrors

    /// Frames an `imgui_input` or `imgui_find` waits for ImGui before it fails with `imgui_timeout`
    inline constexpr uint32_t kMaxImGuiWaitFrames = 600;
    /// `limit` of `imgui_find` when the request has none, and the most it may ask for
    inline constexpr int64_t kDefaultImGuiFindLimit = 50;
    inline constexpr int64_t kMaxImGuiFindLimit = 500;

    /**
     * @brief Registers the control API methods `imgui_input` and `imgui_find`, which drive and read the editor's ImGui.
     *
     * `imgui_input {events: [event], space?: "output" (default) or "imgui"}` answers `{events, frames, wantCaptureMouse, wantCaptureKeyboard, wantTextInput}`.
     *
     * `imgui_find {label?, labelContains?, id?, window?: string, kind?: "window" or "item", visibleOnly?: bool, limit?: 1 to kMaxImGuiFindLimit}` answers
     * `{frame, display, wantCapture*, wantTextInput, hoveredWindow?, focusedWindow?, matches, truncated?}`.
     *
     * Both fail with `no_imgui`, `invalid_params` or `imgui_timeout`.
     *
     * An event is `{type: "move", x, y}`, `{type: "click", x?, y?, button?, count?, mods?}`, `{type: "down" / "up", button?}`, `{type: "wheel", dx?, dy?}`,
     * `{type: "key", key, action?, mods?}` or `{type: "text", text}` (`ImGuiInputEvents.h`). `imgui_input` answers after ImGui has taken the whole queue and one frame
     * has reacted; it runs the frames itself, one of zero seconds at a time, so the manual clock needs no `step`. `imgui_find` arms the item index (`ImGuiItemIndex`)
     * for one frame and answers from that frame's list, with the rects in output pixels, so the centre of a match goes straight into `imgui_input` and `click_at`.
     * With no filter it lists the windows only. Both are `no_imgui` when no ImGui context exists.
     *
     * Registering twice replaces the handlers.
     */
    void RegisterImGuiInputControl(ControlService& service);

}  // namespace BECore
