#include "DemoGame.h"

#include <Demo/DemoNames.h>
#include <Generated/DemoGame.gen.hpp>

namespace BECore::Demo {

    PoolString DemoGame::GetName() const {
        return DemoNames::game;
    }

    PoolString DemoGame::GetSceneName() const {
        return DemoNames::scene;
    }

}  // namespace BECore::Demo
