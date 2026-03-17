#pragma once

#include <Events/EventBase.h>

namespace BECore::SceneEvents {

    struct SceneUpdateEvent : public EventBase<SceneUpdateEvent> {
        BE_EVENT(SceneUpdateEvent)
    };

    struct SceneDrawEvent : public EventBase<SceneDrawEvent> {
        BE_EVENT(SceneDrawEvent)
    };

}  // namespace BECore::SceneEvents
