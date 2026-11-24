#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::Tests {

    /// Overrides OnDraw(): in the draw list by the default of HasOnDraw(). Counts its calls and, with a @c marker of 0 or more, draws a rect at x = marker.
    class OnDrawTestComponent : public IComponent {
        BE_CLASS(OnDrawTestComponent)
    public:
        OnDrawTestComponent() = default;
        ~OnDrawTestComponent() override = default;

        int drawn = 0;
        float marker = -1.0f;

        void OnDraw() override;
    };

    /// Says it never draws: stays out of the draw list, so its OnDraw() is never called
    class SilentTestComponent : public IComponent {
        BE_CLASS(SilentTestComponent)
    public:
        SilentTestComponent() = default;
        ~SilentTestComponent() override = default;

        int drawn = 0;

        bool HasOnDraw() const override {
            return false;
        }
        void OnDraw() override {
            ++drawn;
        }
    };

    /// Overrides nothing: in the draw list too, false is an opt-out
    class PlainTestComponent : public IComponent {
        BE_CLASS(PlainTestComponent)
    public:
        PlainTestComponent() = default;
        ~PlainTestComponent() override = default;
    };

}  // namespace BECore::Tests
