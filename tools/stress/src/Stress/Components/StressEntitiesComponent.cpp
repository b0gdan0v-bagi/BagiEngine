#include "StressEntitiesComponent.h"

#include <BECore/Animation/TweenManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Events/SceneEvents.h>
#include <Generated/StressEntitiesComponent.gen.hpp>
#include <Stress/StressGame.h>

namespace BECore::Stress {

    namespace {

        constexpr float kFullTurnDegrees = 360.0f;

        /// The one name of every node: a name is a PoolString, so a million nodes share it
        PoolString EntityNodeName() {
            return "Entity"_intern;
        }

    }  // namespace

    void StressEntitiesComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &StressEntitiesComponent::OnUpdate>(this);
    }

    void StressEntitiesComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        const StressGame* game = StressGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const StressState& state = game->GetState();
        if (state.Version() != _builtVersion) {
            _builtVersion = state.Version();
            Rebuild(state.GetCount());
        }

        const float step = kSpinDegreesPerSecond * event.deltaTime;
        for (size_t i = 0; i < _entities.size(); i += kSpinEvery) {
            float rotation = _entities[i].transform->_rotation + step;
            if (rotation >= kFullTurnDegrees) {
                rotation -= kFullTurnDegrees;
            }
            _entities[i].transform->_rotation = rotation;
        }
    }

    void StressEntitiesComponent::Rebuild(uint32_t count) {
        _view = FieldView{GetScene().GetViewWidth(), GetScene().GetViewHeight()};
        const size_t target = count;

        // Shrink from the tail: the tween goes first, its setter writes a transform that the node is about to lose (rule 8)
        while (_entities.size() > target) {
            _entities.back().tween.Reset();
            GetNode().RemoveChild(_entities.back().node.Get());
            _entities.pop_back();
        }

        _entities.reserve(target);
        while (_entities.size() < target) {
            Entity entity;
            entity.node = GetNode().AddChild(EntityNodeName());
            entity.transform = New<TransformComponent>();
            entity.node->AddComponent(entity.transform);
            _entities.push_back(std::move(entity));
            StartTween(static_cast<uint32_t>(_entities.size() - 1), true);
        }
    }

    void StressEntitiesComponent::StartTween(uint32_t index, bool first) {
        const EntityOrbit orbit = OrbitOf(index, _view);
        const IntrusivePtr<TransformComponent> transform = _entities[index].transform;
        // The first tween begins at the node's hashed point of its turn, so the ends of a field that was just built are spread; a restart is a full turn
        const float from = first ? orbit.startTurn : 0.0f;
        // The restart captures the index: the nodes are only added and removed at the tail, and a node's tween is reset before it goes
        _entities[index].tween.Reset(CoreManager::GetTweens().Start<float>(
            from, 1.0f, {.duration = orbit.seconds * (1.0f - from), .ease = EaseType::Linear},
            [transform, orbit](const float& turn) {
                const OrbitPoint point = PointOn(orbit, turn);
                transform->_x = point.x;
                transform->_y = point.y;
            },
            [this, index] {
                ++_restartCount;
                StartTween(index, false);
            }));
    }

}  // namespace BECore::Stress
