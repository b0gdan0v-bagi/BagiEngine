#include "GamesConfig.h"

#include <BECore/Cook/ContentCooker.h>
#include <Generated/GamesConfig.gen.hpp>

namespace BECore {

    namespace {

        // Read by `GameManager::Initialize` through the reflection: the cook gives a game's `GamesConfig.xml` the same type.
        const CookableConfig<GamesConfig> kGamesConfig{"GamesConfig"};

    }  // namespace

}  // namespace BECore
