#include "Match3Game.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <Generated/Match3Game.gen.hpp>
#include <Match3/Components/Match3BoardComponent.h>

namespace BECore::Match3 {

    Match3Game::Match3Game()
        : _control([]() {
              if (auto* board = Match3BoardComponent::GetActive()) {
                  Match3ControlTarget target{&board->GetModel(), board->GetSeed()};
                  // The game opens on its menu; a controller that resets or plays is in the game, and a screenshot has to show the board
                  target.enterGame = [board]() { CoreManager::GetSceneManager().SetActiveScene(board->GetScene().GetName()); };
                  return target;
              }
              return Match3ControlTarget{};
          }) {}

    IControllableGame* Match3Game::AsControllable() {
        return &_control;
    }

    PoolString Match3Game::GetName() const {
        return "Match3"_intern;
    }

    PoolString Match3Game::GetSceneName() const {
        // The game opens on its main menu; Play switches to Match3Scene
        return "MainMenu"_intern;
    }

    void Match3Game::Start() {
        if (auto* board = Match3BoardComponent::GetActive()) {
            board->GetModel().Reset(board->GetSeed());
        }
    }

    void Match3Game::Stop() {
        // Nothing scene-specific yet — the component owns its subscriptions.
    }

    void Match3Game::Reset() {
        if (auto* board = Match3BoardComponent::GetActive()) {
            board->GetModel().Reset(board->GetSeed());
        }
    }

}  // namespace BECore::Match3
