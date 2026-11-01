#include "IControllableGame.h"

namespace BECore {

    std::expected<void, ControlError> IControllableGame::SetState(const Json&) {
        return std::unexpected(MakeControlError(GameControlErrors::kUnsupported, "this game cannot load a state"));
    }

}  // namespace BECore
