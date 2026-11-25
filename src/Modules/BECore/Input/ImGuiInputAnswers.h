#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    // The answers of `imgui_input` and `imgui_find` (`ImGuiInputControl.cpp`). A float of ImGui is a double here, so the text of the widened float stays

    /// `imgui_input`: what ImGui made of the request, read after the frame that followed the last event
    struct ImGuiInputAnswer {
        BE_CLASS(ImGuiInputAnswer)

        /// How many events the request held
        BE_REFLECT_FIELD int64_t events = 0;
        /// How many frames ran until the answer
        BE_REFLECT_FIELD int64_t frames = 0;
        BE_REFLECT_FIELD bool wantCaptureKeyboard = false;
        BE_REFLECT_FIELD bool wantCaptureMouse = false;
        BE_REFLECT_FIELD bool wantTextInput = false;
    };

    /// The output ImGui draws to: `width` and `height` in output pixels (the pixels of `screenshot`), `scaleX` and `scaleY` the output pixels per ImGui unit
    struct ImGuiDisplayAnswer {
        BE_CLASS(ImGuiDisplayAnswer)

        BE_REFLECT_FIELD double height = 0.0;
        BE_REFLECT_FIELD double scaleX = 1.0;
        BE_REFLECT_FIELD double scaleY = 1.0;
        BE_REFLECT_FIELD double width = 0.0;
    };

    /// One window or item of `imgui_find`; the rect is in output pixels
    struct ImGuiMatchAnswer {
        BE_CLASS(ImGuiMatchAnswer)

        BE_REFLECT_FIELD double height = 0.0;
        BE_REFLECT_FIELD bool hovered = false;
        /// ImGui's id as hex text, `0x1A2B3C4D`
        BE_REFLECT_FIELD ArenaString id;
        BE_REFLECT_FIELD PoolString kind;
        /// The text the widget was given, `##` suffix included; empty when ImGui did not report one (a clipped button)
        BE_REFLECT_FIELD TextView label;
        BE_REFLECT_FIELD bool visible = false;
        BE_REFLECT_FIELD double width = 0.0;
        /// The window the item was added to; a window names itself
        BE_REFLECT_FIELD TextView window;
        BE_REFLECT_FIELD double x = 0.0;
        BE_REFLECT_FIELD double y = 0.0;
    };

    struct ImGuiFindAnswer {
        BE_CLASS(ImGuiFindAnswer)

        BE_REFLECT_FIELD ImGuiDisplayAnswer display;
        BE_REFLECT_FIELD Optional<ArenaString> focusedWindow;
        /// The ImGui frame number the list is from
        BE_REFLECT_FIELD int64_t frame = 0;
        BE_REFLECT_FIELD Optional<ArenaString> hoveredWindow;
        BE_REFLECT_FIELD ArenaVector<ImGuiMatchAnswer> matches;
        /// Only when more matched than `limit`
        BE_REFLECT_FIELD Optional<bool> truncated;
        BE_REFLECT_FIELD bool wantCaptureKeyboard = false;
        BE_REFLECT_FIELD bool wantCaptureMouse = false;
        BE_REFLECT_FIELD bool wantTextInput = false;
    };

}  // namespace BECore
