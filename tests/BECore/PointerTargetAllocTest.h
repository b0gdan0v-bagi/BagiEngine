#pragma once

#include <BECore/Input/PointerEvent.h>
#include <BECore/Scene/Components/PointerTargetComponent.h>
#include <cstdint>

namespace BECore::Tests {

    /// What the callbacks of AllocTargetComponent saw, kept in plain integers so a callback allocates nothing
    struct AllocTargetCounters {
        uint32_t presses = 0;
        uint32_t releases = 0;
        uint32_t enters = 0;
        uint32_t exits = 0;
        uint32_t moves = 0;
        uint32_t drags = 0;
        uint32_t wheels = 0;
    };

    /// A PointerTargetComponent that takes the press, the motion and the wheel and only counts them
    class AllocTargetComponent : public PointerTargetComponent {
        BE_CLASS(AllocTargetComponent)
    public:
        AllocTargetComponent() = default;
        ~AllocTargetComponent() override = default;

        static AllocTargetCounters counters;

    protected:
        bool OnPointerPress(const PointerEvent& event) override;
        void OnPointerRelease(const PointerEvent& event) override;
        void OnPointerEnter(const PointerEvent& event) override;
        void OnPointerExit(const PointerEvent& event) override;
        bool OnPointerMove(const PointerEvent& event) override;
        void OnPointerDrag(const PointerEvent& event) override;
        bool OnPointerWheel(const PointerEvent& event) override;
    };

}  // namespace BECore::Tests
