#pragma once

#include <Events/EventBase.h>

namespace BECore {
namespace ApplicationEvents {

    // События приложения
    struct QuitEvent : public EventBase<QuitEvent> {};
    struct ApplicationCleanUpEvent : public EventBase<ApplicationCleanUpEvent> {};

}  // namespace ApplicationEvents
}  // namespace BECore

