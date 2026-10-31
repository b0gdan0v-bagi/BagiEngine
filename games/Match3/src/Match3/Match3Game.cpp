#include "Match3Game.h"

#include <Generated/Match3Game.gen.hpp>
#include <Match3/Components/Match3BoardComponent.h>

namespace BECore::Match3 {

    PoolString Match3Game::GetName() const {
        return "Match3"_intern;
    }

    PoolString Match3Game::GetSceneName() const {
        return "Match3Scene"_intern;
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
