#pragma once

#include <Events/EventBase.h>

namespace BECore {
    namespace RenderEvents {

        struct NewFrameEvent : public EventBase<NewFrameEvent> {};
        struct RenderPresentEvent : public EventBase<RenderPresentEvent> {};

        // Emitted by ImGuiWidget, handled by every renderer backend.
        struct ImGuiInitEvent : public EventBase<ImGuiInitEvent> {};
        struct ImGuiShutdownEvent : public EventBase<ImGuiShutdownEvent> {};
        struct ImGuiNewFrameEvent : public EventBase<ImGuiNewFrameEvent> {};
        struct ImGuiRenderEvent : public EventBase<ImGuiRenderEvent> {};

    }  // namespace RenderEvents
}  // namespace BECore
