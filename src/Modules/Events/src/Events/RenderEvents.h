#pragma once

#include <BECore/Math/Color.h>
#include <Events/EventBase.h>

namespace BECore {
    namespace RenderEvents {

        struct NewFrameEvent : public EventBase<NewFrameEvent> {};
        struct RenderClearEvent : public EventBase<RenderClearEvent> {};
        struct RenderPresentEvent : public EventBase<RenderPresentEvent> {};

        // Emitted by ImGuiWidget, handled by every renderer backend.
        struct ImGuiInitEvent : public EventBase<ImGuiInitEvent> {};
        struct ImGuiShutdownEvent : public EventBase<ImGuiShutdownEvent> {};
        struct ImGuiNewFrameEvent : public EventBase<ImGuiNewFrameEvent> {};
        struct ImGuiRenderEvent : public EventBase<ImGuiRenderEvent> {};

        struct SetRenderDrawColorEvent : public EventBase<SetRenderDrawColorEvent> {
            constexpr explicit SetRenderDrawColorEvent(BECore::Color color_) : color(color_) {}
            constexpr explicit SetRenderDrawColorEvent(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : color(BECore::Color{r, g, b, a}) {}
            BECore::Color color;
        };

    }  // namespace RenderEvents
}  // namespace BECore
