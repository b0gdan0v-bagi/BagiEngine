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
     * of `Animation/CLAUDE.md`, and one handle from `TweenManager::Start`, so a frame in which some tween ends allocates; at the benchmark counts
     * some tween ends every frame). The first tween of a node starts at a hashed point of its turn (`EntityOrbit::startTurn`), so a field just
     * built restarts at the steady rate from its first frames.
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

        /// The tweens that ended and were started again since the component was made (a test counts them per frame)
        uint64_t GetRestartCount() const {
            return _restartCount;
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
        /// Starts the tween of node @p index: @p first begins at the node's `startTurn`, a restart at 0
        void StartTween(uint32_t index, bool first);

        eastl::vector<Entity> _entities;
        FieldView _view;
        uint64_t _restartCount = 0;
        uint64_t _builtVersion = 0;  // the StressState version the nodes were last brought to; 0 is never a version
    };

}  // namespace BECore::Stress
