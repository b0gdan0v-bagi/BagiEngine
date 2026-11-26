#pragma once

#include <BECore/Animation/Easing.h>
#include <BECore/Animation/ScopedTween.h>
#include <BECore/Scene/IComponent.h>

namespace BECore::Demo {

    /**
     * @brief Moves and turns its node to and fro for ever: from where the node is to that place plus the offset and the turn, and back.
     *
     * The run is one endless yoyo tween of CoreManager::GetTweens(). A field edited while it runs starts it again; with equal fields a frame
     * starts nothing and allocates nothing. The transform is written as a change of what this component added to it: its last addition is
     * replaced by the new one. So a value that somebody else writes (a drag or a typed x in the editor) stays, and the node moves around it.
     * While the UI variable DemoNames::animateVar says off, the component holds no tween and adds nothing.
     */
    class DemoTweenComponent : public IComponent {
        BE_CLASS(DemoTweenComponent, ICON = move)
    public:
        DemoTweenComponent() = default;
        ~DemoTweenComponent() override = default;

        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(-1280, 1280), tooltip("How far to the right the far end is, in scene units")) float _offsetX = 0.0f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(-720, 720), tooltip("How far down the far end is, in scene units")) float _offsetY = 0.0f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(-720, 720), tooltip("The turn at the far end, in degrees clockwise")) float _turn = 0.0f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0.1, 30), step(0.1), tooltip("Seconds of one way")) float _duration = 1.0f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 30), step(0.1), tooltip("Seconds before the first move of a run")) float _delay = 0.0f;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("The curve of one way")) EaseType _ease = EaseType::SineInOut;

        bool HasOnDraw() const override {
            return false;
        }

        void OnAttached() override;

        /// The tween of the current run, empty while the component holds none
        [[nodiscard]] const IntrusivePtr<TweenHandle>& GetTween() const {
            return _tween.Get();
        }

    private:
        /// The fields a run was started with
        struct Settings {
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            float turn = 0.0f;
            float duration = 0.0f;
            float delay = 0.0f;
            EaseType ease = EaseType::Linear;

            [[nodiscard]] bool operator==(const Settings&) const = default;
        };

        /// One value of the transform that the component adds to. It keeps the value under its addition, so taking the addition back gives that
        /// value exactly, without the rounding that subtracting it again every frame would pile up
        struct Addition {
            float under = 0.0f;    ///< the value without the addition
            float amount = 0.0f;   ///< what is added now
            float written = 0.0f;  ///< what was written last; another value in the transform is somebody else's edit

            void Apply(float& value, float newAmount);
        };

        void OnUpdate(const SceneEvents::SceneUpdateEvent& e);
        void Start(const Settings& settings);

        Settings _running;
        Addition _x;
        Addition _y;
        Addition _rotation;
        float _progress = 0.0f;  // 0 at the start of the way, 1 at the far end; the tween writes it, the update places the node
        ScopedTween _tween;      // after _progress: its setter points there
    };

}  // namespace BECore::Demo
