#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Counts the updates and draws its component receives.
     *
     * drawIndex is the position of its last OnDraw call in the draw pass (0 for the first component drawn), so a test
     * can compare the draw order of several nodes. Reflected so a SceneConfig-style XML can create it by type name
     * (see SceneEventsTest.cpp).
     */
    class SceneEventCounter : public IComponent {
        BE_CLASS(SceneEventCounter)
    public:
        SceneEventCounter() = default;
        ~SceneEventCounter() override = default;

        int updateCount = 0;
        int drawCount = 0;
        int drawIndex = -1;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);
    };

}  // namespace BECore::Tests
