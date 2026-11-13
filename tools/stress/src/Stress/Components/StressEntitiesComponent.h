#pragma once

#include <BECore/Animation/ScopedTween.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/IComponent.h>
#include <Stress/EntityField.h>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Stress {

    /**
     * @brief The scene `StressEntities`: `StressState::GetCount()` child nodes of this node, each with a `TransformComponent` and one running
     *        tween that moves it on a circle of its own. No node has a renderer component, so nothing is drawn: the scene costs the update
     *        of the nodes, the tweens and the draw system's walk.
     *
     * The nodes are made like `StressSpritesComponent`'s: when `StressState::Version()` moved, appended or removed from the tail. A tween is a
     * `Tween<float>` of 0..1 whose setter writes the position, and its `onComplete` starts it again (a restart costs one frame per link, rule 2
     * of `Animation/CLAUDE.md`, and one handle: the only allocation of a steady frame, on the frames some tween ends; the durations are long so few do).
     * A shrink resets the tail's tweens first (rule 8: the setter must not outlive what it writes). Every `kSpinEvery`th node also gets a rotation
     * from `SceneUpdateEvent`. Does nothing while the game is not the active `StressGame`.
     */
    class StressEntitiesComponent : public IComponent {
        BE_CLASS(StressEntitiesComponent)
    public:
        StressEntitiesComponent() = default;
        ~StressEntitiesComponent() override = default;

        void OnAttached() override;

        /// The nodes made so far (a test counts them)
        size_t GetEntityCount() const {
            return _entities.size();
        }

    private:
        struct Entity {
            IntrusivePtr<SceneNode> node;
            IntrusivePtr<TransformComponent> transform;
            ScopedTween tween;
        };

        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        /// Brings the nodes to @p count: appends or removes from the tail
        void Rebuild(uint32_t count);
        /// Starts the tween of node @p index (again when the last one ended)
        void StartTween(uint32_t index);

        eastl::vector<Entity> _entities;
        FieldView _view;
        uint64_t _builtVersion = 0;  // the StressState version the nodes were last brought to; 0 is never a version
    };

}  // namespace BECore::Stress
