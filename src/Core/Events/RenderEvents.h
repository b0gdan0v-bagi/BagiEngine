#pragma once

#include <Core/Events/EventBase.h>

namespace Math {
    struct Color;
}

namespace Core {
namespace RenderEvents {

    // События рендеринга
    struct NewFrameEvent : public EventBase<NewFrameEvent> {};
    struct RenderClearEvent : public EventBase<RenderClearEvent> {};
    struct RenderPresentEvent : public EventBase<RenderPresentEvent> {};
    
    struct SetRenderDrawColorEvent : public EventBase<SetRenderDrawColorEvent> {
        constexpr explicit SetRenderDrawColorEvent(Math::Color color_) : color(color_) {}
        constexpr explicit SetRenderDrawColorEvent(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : color(Math::Color{r, g, b, a}) {}
        Math::Color color;
    };

}  // namespace RenderEvents
}  // namespace Core

