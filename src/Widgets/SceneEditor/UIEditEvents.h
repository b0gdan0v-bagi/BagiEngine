#pragma once

#include <BECore/UI/UIRectSpec.h>
#include <Events/EventBase.h>

namespace BECore {

    class Scene;
    class SceneNode;
    class UIElement;

    /**
     * @brief The rect of a canvas element was changed by the inspector: the hook for an undo stack
     *
     * Sent by SceneEditorWidget in every frame in which the inspector reports a change, so one drag of a field is a run of events; an undo stack merges the frames of one gesture. @c before and
     * @c after are the authored `style.rect` of the frame's start and end, held by value. The pointers are valid for the frame of the event only: a subscriber that keeps an edit copies the
     * specs and finds the element again through its node before it writes to it. Emitted, not enqueued.
     */
    struct UIRectEditEvent : public EventBase<UIRectEditEvent> {
        const Scene* scene = nullptr;
        const SceneNode* node = nullptr;
        const UIElement* element = nullptr;
        UIRectSpec before;
        UIRectSpec after;

        UIRectEditEvent() = default;
        UIRectEditEvent(const Scene* editScene, const SceneNode* editNode, const UIElement* editElement, const UIRectSpec& rectBefore, const UIRectSpec& rectAfter)
            : scene(editScene), node(editNode), element(editElement), before(rectBefore), after(rectAfter) {}
    };

}  // namespace BECore
