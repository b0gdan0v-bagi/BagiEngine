#pragma once

#include <Events/EventBase.h>

namespace BECore {
    namespace ApplicationEvents {

        struct QuitEvent : public EventBase<QuitEvent> {};
        struct ApplicationCleanUpEvent : public EventBase<ApplicationCleanUpEvent> {};

    }  // namespace ApplicationEvents
}  // namespace BECore
