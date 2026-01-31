#pragma once

#include <Events/EventBase.h>

namespace BECore {
namespace RenderEvents {

    // События рендеринга
    struct NewFrameEvent : public EventBase<NewFrameEvent> {};
    struct RenderClearEvent : public EventBase<RenderClearEvent> {};
    struct RenderPresentEvent : public EventBase<RenderPresentEvent> {};
    
    struct SetRenderDrawColorEvent : public EventBase<SetRenderDrawColorEvent> {
        constexpr explicit SetRenderDrawColorEvent(BECore::Color color_) : color(color_) {}
        constexpr explicit SetRenderDrawColorEvent(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : color(BECore::Color{r, g, b, a}) {}
        BECore::Color color;
    };

}  // namespace RenderEvents
}  // namespace BECore

