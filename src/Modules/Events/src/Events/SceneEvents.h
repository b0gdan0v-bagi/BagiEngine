#pragma once

#include <BECore/Scene/SceneView.h>
#include <Events/EventBase.h>

namespace BECore {
    class Scene;
}  // namespace BECore

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

    /**
     * @brief The scene's active view (Scene::GetActiveView()) changed since this event was last sent.
     *
     * Emitted from Scene::Update() (never from Draw()), once per change however many frames the view changed in, so a handler may change the
     * scene. `view` is the view in effect at that moment. Global like the other scene events: filter on `scene` or use SubscribeInScene.
     */
    struct SceneViewChangedEvent : public EventBase<SceneViewChangedEvent> {
        const Scene* scene = nullptr;
        ActiveSceneView view;

        SceneViewChangedEvent() = default;
        SceneViewChangedEvent(const Scene* s, const ActiveSceneView& v) : scene(s), view(v) {}
    };

}  // namespace BECore::SceneEvents
