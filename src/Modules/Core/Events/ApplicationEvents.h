#pragma once

#include <Core/Events/EventBase.h>

namespace Core {
namespace ApplicationEvents {

    // События приложения
    struct QuitEvent : public EventBase<QuitEvent> {};
    struct ApplicationCleanUpEvent : public EventBase<ApplicationCleanUpEvent> {};

}  // namespace ApplicationEvents
}  // namespace Core

