#include "Match3MenuComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <Events/ApplicationEvents.h>
#include <Events/InputEvents.h>
#include <Generated/Match3MenuComponent.gen.hpp>
#include <Match3/Components/Match3BoardComponent.h>

namespace BECore::Match3 {

    void Match3MenuComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &Match3MenuComponent::OnAction>(this);
    }

    void Match3MenuComponent::OnAction(const InputEvents::ActionEvent& event) {
        // The actions of the buttons in config/SceneConfig.xml
        const PoolString playAction = "Match3Play"_intern;
        const PoolString quitAction = "Match3Quit"_intern;
        if (event.action == playAction) {
            if (auto* board = Match3BoardComponent::GetActive()) {
                board->GetModel().Reset(board->GetSeed());
            }
            if (!_gameScene.Empty()) {
                CoreManager::GetSceneManager().SetActiveScene(_gameScene);
            }
        } else if (event.action == quitAction) {
            ApplicationEvents::QuitEvent::Emit();
        }
    }

}  // namespace BECore::Match3
