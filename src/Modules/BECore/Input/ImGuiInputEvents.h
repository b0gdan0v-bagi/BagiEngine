#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>

struct ImGuiIO;

namespace BECore {

    /// What one entry of a request does
    CORE_ENUM(ImGuiInputKind, uint8_t, Move, Click, Down, Up, Wheel, Key, Text)

    /// The order is ImGui's own mouse button index
    CORE_ENUM(ImGuiInputButton, uint8_t, Left, Right, Middle)

    /// `Press` is the key down and then up; `Down` and `Up` change the key's state only
    CORE_ENUM(ImGuiKeyAction, uint8_t, Press, Down, Up)

    /// The unit of a position: render output pixels (the pixels of `screenshot` and `click_at`) or ImGui's own window units
    CORE_ENUM(ImGuiInputSpace, uint8_t, Output, Window)

    /// Modifiers held around an event, named as the application sees them (`ImGuiMod_*`): `ctrl` is Cmd on macOS, what the editor's
    /// `KeyChord` calls Ctrl, and `super` is the physical Ctrl there
    struct ImGuiInputMods {
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        bool super = false;
    };

    /// One entry of an `imgui_input` request. Each kind reads only its own fields
    struct ImGuiInputEvent {
        ImGuiInputKind kind = ImGuiInputKind::Move;
        /// `Move` needs it; `Click` moves first when it is set and clicks where the pointer is when not
        bool hasPosition = false;
        float x = 0.0f;
        float y = 0.0f;
        /// `Click`, `Down`, `Up`
        ImGuiInputButton button = ImGuiInputButton::Left;
        /// `Click`: how many presses in a row, 1 to `kMaxImGuiClickCount`; ImGui reads a double click from the clock, not from this
        int count = 1;
        /// `Click` and a `Key` press
        ImGuiInputMods mods;
        /// `Wheel`, in ImGui's wheel steps; positive `dy` scrolls up (the sign SDL sends)
        float dx = 0.0f;
        float dy = 0.0f;
        /// `Key`: one of ImGui's key names (`Enter`, `A`, `F6`, `LeftArrow`), any letter case
        String key;
        ImGuiKeyAction action = ImGuiKeyAction::Press;
        /// `Text`: UTF-8
        String text;
    };

    /// At most this many events in one request
    constexpr size_t kMaxImGuiInputEvents = 256;
    /// `count` of a click
    constexpr int kMaxImGuiClickCount = 3;
    /// A longer text is refused
    constexpr size_t kMaxImGuiInputTextBytes = 1024;
    /// A position or a wheel step beyond this is refused, far beyond any screen
    constexpr float kMaxImGuiInputMagnitude = 1.0e6f;

    /// Checks a whole request without touching ImGui. The error names the index of the first bad event and why
    [[nodiscard]] std::expected<void, String> ValidateImGuiEvents(Span<const ImGuiInputEvent> events);

    /**
     * @brief Validates the request, then expands it into `io.Add*Event` calls in order. A bad request leaves `io` untouched.
     *
     * Positions are divided by `io.DisplayFramebufferScale` when `space` is `Output`. ImGui takes one change of a mouse button,
     * one change of a key or one wheel step per frame from its queue (`ConfigInputTrickleEventQueue`), and a click is three
     * frames; the request is not done until `ImGuiInputQueueEmpty`.
     */
    [[nodiscard]] std::expected<void, String> QueueImGuiEvents(ImGuiIO& io, Span<const ImGuiInputEvent> events, ImGuiInputSpace space);

    /// Whether ImGui has taken every queued event into its state
    [[nodiscard]] bool ImGuiInputQueueEmpty(const ImGuiIO& io);

}  // namespace BECore
