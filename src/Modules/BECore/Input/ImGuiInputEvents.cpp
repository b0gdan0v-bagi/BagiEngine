#include "ImGuiInputEvents.h"

#include <SDL3/SDL_stdinc.h>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore {

    namespace {

        char LowerAscii(char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsIgnoreCase(StringView lhs, StringView rhs) {
            if (lhs.size() != rhs.size()) {
                return false;
            }
            for (size_t i = 0; i < lhs.size(); ++i) {
                if (LowerAscii(lhs[i]) != LowerAscii(rhs[i])) {
                    return false;
                }
            }
            return true;
        }

        /// A keyboard key by its ImGui name. The mouse and gamepad entries of ImGui's key list are not keys of this request; a scan of
        /// ~140 names per key event is cheaper than a table that needs a lifetime
        Optional<ImGuiKey> FindKey(StringView name) {
            for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; ++key) {
                if (EqualsIgnoreCase(ImGui::GetKeyName(static_cast<ImGuiKey>(key)), name)) {
                    return static_cast<ImGuiKey>(key);
                }
            }
            return {};
        }

        bool IsUsableNumber(float value) {
            return std::isfinite(value) && std::fabs(value) <= kMaxImGuiInputMagnitude;
        }

        /// The reason ImGui cannot take the text, or null: it must be UTF-8 without an encoding error and without a NUL (ImGui reads a C string)
        const char* CheckText(StringView text) {
            if (text.empty()) {
                return "the text is empty";
            }
            if (text.size() > kMaxImGuiInputTextBytes) {
                return "the text is too long";
            }
            const char* cursor = text.data();
            size_t remaining = text.size();
            while (remaining > 0) {
                const Uint32 codepoint = SDL_StepUTF8(&cursor, &remaining);
                // SDL reports an encoding error as U+FFFD, so a text that really holds U+FFFD is refused too
                if (codepoint == 0 || codepoint == SDL_INVALID_UNICODE_CODEPOINT) {
                    return "the text is not valid UTF-8 or holds a NUL";
                }
            }
            return nullptr;
        }

        /// The reason the event is refused, or null
        const char* CheckEvent(const ImGuiInputEvent& event) {
            switch (event.kind) {
                case ImGuiInputKind::Move:
                    if (!event.hasPosition) {
                        return "move needs x and y";
                    }
                    break;
                case ImGuiInputKind::Click:
                    if (event.count < 1 || event.count > kMaxImGuiClickCount) {
                        return "the click count is out of range";
                    }
                    break;
                case ImGuiInputKind::Down:
                case ImGuiInputKind::Up:
                    break;
                case ImGuiInputKind::Wheel:
                    if (!IsUsableNumber(event.dx) || !IsUsableNumber(event.dy)) {
                        return "the wheel step is not a finite number in range";
                    }
                    if (event.dx == 0.0f && event.dy == 0.0f) {
                        return "wheel needs dx or dy";
                    }
                    break;
                case ImGuiInputKind::Key:
                    if (!FindKey(event.key)) {
                        return "unknown key name";
                    }
                    break;
                case ImGuiInputKind::Text:
                    return CheckText(event.text);
            }
            const bool positioned = event.kind == ImGuiInputKind::Move || (event.kind == ImGuiInputKind::Click && event.hasPosition);
            if (positioned && (!IsUsableNumber(event.x) || !IsUsableNumber(event.y))) {
                return "the position is not a finite number in range";
            }
            const bool holdsMods = event.mods.ctrl || event.mods.shift || event.mods.alt || event.mods.super;
            if (event.kind == ImGuiInputKind::Key && event.action != ImGuiKeyAction::Press && holdsMods) {
                return "mods go with a key press only";
            }
            return nullptr;
        }

        /// The key to hand `AddKeyEvent` so that ImGui's own modifier `appLevel` is the one that goes down. `AddKeyEvent` takes the
        /// physical key and swaps Ctrl and Super on macOS, so the swap is made here once more
        ImGuiKey PhysicalMod(const ImGuiIO& io, ImGuiKey appLevel) {
            if (io.ConfigMacOSXBehaviors) {
                if (appLevel == ImGuiMod_Ctrl) {
                    return ImGuiMod_Super;
                }
                if (appLevel == ImGuiMod_Super) {
                    return ImGuiMod_Ctrl;
                }
            }
            return appLevel;
        }

        void SetMods(ImGuiIO& io, const ImGuiInputMods& mods, bool down) {
            if (mods.ctrl) {
                io.AddKeyEvent(PhysicalMod(io, ImGuiMod_Ctrl), down);
            }
            if (mods.shift) {
                io.AddKeyEvent(ImGuiMod_Shift, down);
            }
            if (mods.alt) {
                io.AddKeyEvent(ImGuiMod_Alt, down);
            }
            if (mods.super) {
                io.AddKeyEvent(PhysicalMod(io, ImGuiMod_Super), down);
            }
        }

        /// ImGui's units per output pixel factor; a backend that never set the scale leaves 0
        float UnitsPerPixel(float framebufferScale) {
            return std::isfinite(framebufferScale) && framebufferScale > 0.0f ? 1.0f / framebufferScale : 1.0f;
        }

        void QueueMove(ImGuiIO& io, const ImGuiInputEvent& event, ImGuiInputSpace space) {
            const bool output = space == ImGuiInputSpace::Output;
            const float unitsX = output ? UnitsPerPixel(io.DisplayFramebufferScale.x) : 1.0f;
            const float unitsY = output ? UnitsPerPixel(io.DisplayFramebufferScale.y) : 1.0f;
            io.AddMousePosEvent(event.x * unitsX, event.y * unitsY);
        }

        void QueueEvent(ImGuiIO& io, const ImGuiInputEvent& event, ImGuiInputSpace space) {
            const int button = static_cast<int>(event.button);
            switch (event.kind) {
                case ImGuiInputKind::Move:
                    QueueMove(io, event, space);
                    break;
                case ImGuiInputKind::Click:
                    SetMods(io, event.mods, true);
                    if (event.hasPosition) {
                        QueueMove(io, event, space);
                    }
                    for (int press = 0; press < event.count; ++press) {
                        io.AddMouseButtonEvent(button, true);
                        io.AddMouseButtonEvent(button, false);
                    }
                    SetMods(io, event.mods, false);
                    break;
                case ImGuiInputKind::Down:
                    io.AddMouseButtonEvent(button, true);
                    break;
                case ImGuiInputKind::Up:
                    io.AddMouseButtonEvent(button, false);
                    break;
                case ImGuiInputKind::Wheel:
                    io.AddMouseWheelEvent(event.dx, event.dy);
                    break;
                case ImGuiInputKind::Key: {
                    const ImGuiKey key = *FindKey(event.key);
                    SetMods(io, event.mods, true);
                    if (event.action != ImGuiKeyAction::Up) {
                        io.AddKeyEvent(key, true);
                    }
                    if (event.action != ImGuiKeyAction::Down) {
                        io.AddKeyEvent(key, false);
                    }
                    SetMods(io, event.mods, false);
                    break;
                }
                case ImGuiInputKind::Text:
                    io.AddInputCharactersUTF8(event.text.c_str());
                    break;
            }
        }

    }  // namespace

    std::expected<void, String> ValidateImGuiEvents(Span<const ImGuiInputEvent> events) {
        if (events.empty()) {
            return std::unexpected(String{"no events"});
        }
        if (events.size() > kMaxImGuiInputEvents) {
            return std::unexpected(Format("too many events ({}, at most {})", events.size(), kMaxImGuiInputEvents));
        }
        for (size_t index = 0; index < events.size(); ++index) {
            if (const char* reason = CheckEvent(events[index])) {
                return std::unexpected(Format("events[{}]: {}", index, reason));
            }
        }
        return {};
    }

    std::expected<void, String> QueueImGuiEvents(ImGuiIO& io, Span<const ImGuiInputEvent> events, ImGuiInputSpace space) {
        if (auto valid = ValidateImGuiEvents(events); !valid) {
            return valid;
        }
        for (const ImGuiInputEvent& event : events) {
            QueueEvent(io, event, space);
        }
        return {};
    }

    bool ImGuiInputQueueEmpty(const ImGuiIO& io) {
        return io.Ctx == nullptr || io.Ctx->InputEventsQueue.empty();
    }

}  // namespace BECore
