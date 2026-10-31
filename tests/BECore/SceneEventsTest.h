#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneDrawEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Counts the scene events its component receives.
     *
     * Reflected so a SceneConfig-style XML can create it by type name (see SceneEventsTest.cpp).
     */
    class SceneEventCounter : public IComponent {
        BE_CLASS(SceneEventCounter)
    public:
        SceneEventCounter() = default;
        ~SceneEventCounter() override = default;

        int updateCount = 0;
        int drawCount = 0;

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);
        void OnDraw(const SceneEvents::SceneDrawEvent&);
    };

}  // namespace BECore::Tests
