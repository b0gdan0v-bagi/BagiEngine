#include "Match3SaveState.h"

#include <Generated/Match3SaveState.gen.hpp>
#include <Match3/Game/Match3GameModel.h>

namespace BECore::Match3 {

    std::expected<void, SaveError> SaveMatch3(SaveGameService& service, eastl::string_view slot, const Match3GameModel& model) {
        return service.Save(slot, kMatch3SaveSchema, model.CaptureState());
    }

    std::expected<SaveInfo, SaveError> LoadMatch3(const SaveGameService& service, eastl::string_view slot, Match3GameModel& model) {
        Match3SaveState state;
        auto info = service.Load(slot, kMatch3SaveSchema, state);
        if (!info) {
            return std::unexpected(eastl::move(info.error()));
        }

        if (auto restored = model.RestoreState(state); !restored) {
            return std::unexpected(SaveError{SaveErrorKind::Corrupt, Format("the Match3 save '{}' cannot be restored: {}", slot, restored.error())});
        }
        return info;
    }

}  // namespace BECore::Match3
