#pragma once

#include <BECore/Input/PointerEvent.h>
#include <BECore/Scene/Components/PointerTargetComponent.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore::Tests {

    CORE_ENUM(TargetCall, uint8_t, Press, Release, Enter, Exit, Move, Drag, Wheel)

    struct TargetRecord {
        TargetCall call = TargetCall::Press;
        PointerEvent event;
    };

    /// What the target does at each callback. Lives outside the component, so a callback that destroys the component still has its log
    struct TargetScript {
        Vector<TargetRecord> log;
        MoveOnlyFunction<void(TargetCall)> hook;  ///< runs inside every callback, after it is logged
        bool takePress = true;
        bool takeMove = true;
        bool takeWheel = false;

        [[nodiscard]] size_t Count(TargetCall call) const {
            size_t count = 0;
            for (const TargetRecord& record : log) {
                count += record.call == call ? 1 : 0;
            }
            return count;
        }
    };

    /// A PointerTargetComponent that logs every callback to its script; `scenePredicate` replaces the shape when set
    class PointerTargetTestComponent : public PointerTargetComponent {
        BE_CLASS(PointerTargetTestComponent)
    public:
        PointerTargetTestComponent() = default;
        ~PointerTargetTestComponent() override = default;

        TargetScript* script = nullptr;
        bool (*scenePredicate)(float sceneX, float sceneY) = nullptr;

    protected:
        [[nodiscard]] bool ContainsScenePoint(float sceneX, float sceneY) const override;
        bool OnPointerPress(const PointerEvent& event) override;
        void OnPointerRelease(const PointerEvent& event) override;
        void OnPointerEnter(const PointerEvent& event) override;
        void OnPointerExit(const PointerEvent& event) override;
        bool OnPointerMove(const PointerEvent& event) override;
        void OnPointerDrag(const PointerEvent& event) override;
        bool OnPointerWheel(const PointerEvent& event) override;

    private:
        /// Logs the call and runs the hook, which may destroy the component: nothing of it is read afterwards
        static void Note(TargetScript* script, TargetCall call, const PointerEvent& event);
    };

}  // namespace BECore::Tests
