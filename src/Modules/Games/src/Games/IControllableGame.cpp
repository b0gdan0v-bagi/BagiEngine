#include "IControllableGame.h"

#include <BECore/Control/Json.h>

namespace BECore {

    std::expected<void, ControlError> IControllableGame::SetState(const Json&) {
        return std::unexpected(MakeControlError(GameControlErrors::kUnsupported, "this game cannot load a state"));
    }

    bool IControllableGame::IsReplayable() const {
        return false;
    }

    Json IControllableGame::GetReplayStart() const {
        return Json::object();
    }

}  // namespace BECore
