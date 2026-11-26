#include "DemoControlsComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/ParticleEmitterComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIValueText.h>
#include <BECore/UI/UIVars.h>
#include <Demo/DemoNames.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/DemoControlsComponent.gen.hpp>

namespace BECore::Demo {

    namespace {

        /// The first node called @p name below @p from, depth first
        SceneNode* FindBelow(SceneNode& from, PoolString name) {
            for (const auto& child : from.GetChildren()) {
                if (child->GetName() == name) {
                    return child.Get();
                }
                if (SceneNode* found = FindBelow(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

    }  // namespace

    void DemoControlsComponent::OnAttached() {
        CoreManager::GetUIVars().Set(DemoNames::animateVar, DemoNames::on);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &DemoControlsComponent::OnUpdate>(this);
        SubscribeInScene<InputEvents::ActionEvent, &DemoControlsComponent::OnAction>(this);
    }

    void DemoControlsComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        if (const auto emitter = FindEmitter()) {
            FollowRate(*emitter);
        }
    }

    void DemoControlsComponent::OnAction(const InputEvents::ActionEvent& e) {
        const StringView value = e.value.ToStringView();
        if (e.action == DemoNames::animateAction) {
            if (value == DemoNames::on || value == DemoNames::off) {
                CoreManager::GetUIVars().Set(DemoNames::animateVar, value);
            }
        } else if (e.action == DemoNames::emissionAction) {
            const Optional<float> rate = UIText::ParseFiniteFloat(value);
            const auto emitter = FindEmitter();
            if (rate && emitter) {
                emitter->_effect.rate = *rate;
                FollowRate(*emitter);
            }
        }
    }

    IntrusivePtr<ParticleEmitterComponent> DemoControlsComponent::FindEmitter() const {
        SceneNode* node = FindBelow(GetScene().GetRootNode(), _emitterNode);
        return node ? node->GetComponent<ParticleEmitterComponent>() : IntrusivePtr<ParticleEmitterComponent>();
    }

    void DemoControlsComponent::FollowRate(ParticleEmitterComponent& emitter) {
        const float rate = emitter._effect.rate;
        if (_publishedRate == rate) {
            return;
        }
        // A looping emitter counts what is due from Play(), so after a lower rate nothing is born until that count passes the particles born at the old one
        if (_publishedRate && rate < *_publishedRate && emitter.IsEmitting()) {
            emitter.Play();
        }
        _publishedRate = rate;
        CoreManager::GetUIVars().Set(DemoNames::emissionVar, rate);
    }

}  // namespace BECore::Demo
