#include "DemoTweenComponent.h"

#include <BECore/Animation/TweenManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIVars.h>
#include <Demo/DemoNames.h>
#include <Events/SceneEvents.h>
#include <Generated/DemoTweenComponent.gen.hpp>

namespace BECore::Demo {

    void DemoTweenComponent::Addition::Apply(float& value, float newAmount) {
        if (value != written) {
            under = value - amount;
        }
        amount = newAmount;
        value = under + amount;
        written = value;
    }

    void DemoTweenComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &DemoTweenComponent::OnUpdate>(this);
    }

    void DemoTweenComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        const Settings settings{_offsetX, _offsetY, _turn, _duration, _delay, _ease};
        if (CoreManager::GetUIVars().Get(DemoNames::animateVar) == DemoNames::off) {
            _tween.Reset();
            _progress = 0.0f;
        } else if (!_tween.Get() || _running != settings) {
            Start(settings);
        }

        if (const auto transform = GetNode().GetComponent<TransformComponent>()) {
            _x.Apply(transform->_x, _running.offsetX * _progress);
            _y.Apply(transform->_y, _running.offsetY * _progress);
            _rotation.Apply(transform->_rotation, _running.turn * _progress);
        }
    }

    void DemoTweenComponent::Start(const Settings& settings) {
        _running = settings;
        _progress = 0.0f;
        Timeline there;
        there.Append<float>(0.0f, 1.0f, {.duration = settings.duration, .ease = settings.ease}, [progress = &_progress](const float& value) { *progress = value; }).Yoyo().RepeatForever();
        _tween.Reset(CoreManager::GetTweens().Play(std::move(there), settings.delay));
    }

}  // namespace BECore::Demo
