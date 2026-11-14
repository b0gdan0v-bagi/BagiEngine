#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Save/SaveGame.h>

namespace BECore::Match3 {

    class Match3GameModel;

    /// The id of the game in a save file (`SaveSchema::game`)
    inline constexpr StringView kMatch3SaveGame = "Match3";

    /// The version of the state schema that is written now
    inline constexpr uint32_t kMatch3SaveVersion = 1;

    /// The oldest version that is still read
    inline constexpr uint32_t kMatch3SaveMinVersion = 1;

    /// What `SaveMatch3` and `LoadMatch3` pass to the save game service
    inline constexpr SaveSchema kMatch3SaveSchema{kMatch3SaveGame, kMatch3SaveVersion, kMatch3SaveMinVersion};

    /**
     * @brief Everything of a Match3 game that a save keeps, as plain reflected data (Match3GameModel::CaptureState / RestoreState).
     *
     * The selected cell and the trace of the last swap are not saved: a restored game has neither. Adding a field needs no
     * version (an old file keeps the default); changing what a field means does (kMatch3SaveVersion).
     */
    struct Match3SaveState {
        BE_CLASS(Match3SaveState)

        /// Seed of the last Reset()
        BE_REFLECT_FIELD uint32_t seed = 0;
        /// Score; not negative
        BE_REFLECT_FIELD int32_t score = 0;
        /// The board row by row (row 0 on top), one letter per gem as in the control API's `get_state` (`ROYGBP`, no empty cell): 64 characters
        BE_REFLECT_FIELD String board;
        /// The state of the random generator as std::mt19937 writes it to a stream: 624 numbers in the standard's form (MSVC, libc++), libstdc++ adds the position, so a save moves between
        /// implementations that write the same form
        BE_REFLECT_FIELD String rng;
    };

    /// Save the game as the slot (`CaptureState` inside the envelope of kMatch3SaveSchema); the errors of SaveGameService::Save
    [[nodiscard]] std::expected<void, SaveError> SaveMatch3(SaveGameService& service, StringView slot, const Match3GameModel& model);

    /**
     * @brief Load the slot into the game
     *
     * The file is read into a temporary Match3SaveState and given to RestoreState(), so the model changes only when the
     * whole save is good. A state that RestoreState() refuses is Corrupt, with its reason as the detail.
     *
     * @return what the file says (its version) or the errors of SaveGameService::Load
     */
    [[nodiscard]] std::expected<SaveInfo, SaveError> LoadMatch3(const SaveGameService& service, StringView slot, Match3GameModel& model);

}  // namespace BECore::Match3
