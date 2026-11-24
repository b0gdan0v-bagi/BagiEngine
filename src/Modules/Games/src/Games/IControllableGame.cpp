#include "IControllableGame.h"

namespace BECore {

    std::expected<void, ControlError> IControllableGame::SetState(JsonView) {
        return std::unexpected(MakeControlError(GameControlErrors::kUnsupported, "this game cannot load a state"));
    }

    bool IControllableGame::IsReplayable() const {
        return false;
    }

    std::expected<void, ControlError> IControllableGame::WriteReplayStart(JsonWriter& writer) const {
        writer.BeginObject();
        writer.EndObject();
        return {};
    }

}  // namespace BECore
