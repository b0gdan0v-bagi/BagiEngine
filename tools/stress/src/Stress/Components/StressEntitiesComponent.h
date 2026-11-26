#pragma once

#include <BECore/Animation/ScopedTween.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>
#include <Stress/EntityField.h>
#include <cstddef>
#include <cstdint>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Stress {

    /**
     * @brief The scene `StressEntities`: `StressState::GetCount()` child nodes of this node, each with a `TransformComponent` and one looping
     *        tween that moves it on a circle of its own. No node has a renderer component, so the scene's draw list is empty and
     *        the scene costs the update of the nodes and the tweens.
     *
     * The nodes are made like `StressSpritesComponent`'s, when `StressState::Version()` moved. The tween is a `TweenManager::Loop`, so a frame
     * with an unchanged count starts no tween and allocates nothing.
     * A shrink resets the tail's tweens first (rule 8 of `Animation/CLAUDE.md`: the setter must not outlive what it writes). Every `kSpinEvery`th
     * node also gets a rotation from `SceneUpdateEvent`. Does nothing while the game is not the active `StressGame`.
     */
    class StressEntitiesComponent : public IComponent {
        BE_CLASS(StressEntitiesComponent)
    public:
        StressEntitiesComponent() = default;
        ~StressEntitiesComponent() override = default;
        bool HasOnDraw() const override {
            return false;
        }

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
        void StartTween(uint32_t index);

        Vector<Entity> _entities;
        FieldView _view;
        uint64_t _builtVersion = 0;  // the StressState version the nodes were last brought to; 0 is never a version
    };

}  // namespace BECore::Stress
