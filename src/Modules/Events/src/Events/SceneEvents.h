#pragma once

#include <Events/EventBase.h>

namespace BECore::SceneEvents {

    /// Once per frame. deltaTime is the frame time in seconds (Time::GetDeltaTime()), zero when the clock has not advanced.
    struct SceneUpdateEvent : public EventBase<SceneUpdateEvent> {
        BE_EVENT(SceneUpdateEvent)
        BE_REFLECT_FIELD float deltaTime = 0.0f;

        SceneUpdateEvent() = default;
        explicit SceneUpdateEvent(float dt) : deltaTime(dt) {}
    };

    /// Zero or more times per frame, before SceneUpdateEvent. deltaTime is the fixed step (Time::GetFixedDeltaTime()), always the same value.
    struct SceneFixedUpdateEvent : public EventBase<SceneFixedUpdateEvent> {
        BE_EVENT(SceneFixedUpdateEvent)
        BE_REFLECT_FIELD float deltaTime = 0.0f;

        SceneFixedUpdateEvent() = default;
        explicit SceneFixedUpdateEvent(float dt) : deltaTime(dt) {}
    };

    struct SceneDrawEvent : public EventBase<SceneDrawEvent> {
        BE_EVENT(SceneDrawEvent)
    };

}  // namespace BECore::SceneEvents
