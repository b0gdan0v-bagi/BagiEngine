#include "IControllableGame.h"

#include <BECore/Control/Json.h>

namespace BECore {

    namespace {

        std::expected<void, ControlError> WriteDocument(JsonWriter& writer, const std::expected<Json, ControlError>& document) {
            if (!document) {
                return std::unexpected(document.error());
            }
            WriteJson(writer, *document);
            return {};
        }

    }  // namespace

    std::expected<void, ControlError> IControllableGame::WriteState(JsonWriter& writer) const {
        return WriteDocument(writer, GetState());
    }

    std::expected<void, ControlError> IControllableGame::WriteActions(JsonWriter& writer) const {
        return WriteDocument(writer, GetActions());
    }

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
