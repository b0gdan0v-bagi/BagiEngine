#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    class IControllableGame;

    /**
     * @brief The interface of a game as JSON text, for code that has no `JsonWriter` or `JsonView` at hand: the Game panel, tests, the tests of a game.
     *
     * Each function calls the interface directly and never records (a recording is made by `ControlledX`, GameControl.h). A text that is not
     * JSON is `invalid_params` with the tape's message; a writer that returns success with an incomplete value is `internal_error`; an error of the game
     * is returned as it is.
     */

    /// WriteState() as compact text
    [[nodiscard]] std::expected<String, ControlError> StateText(const IControllableGame& game);

    /// WriteActions() as compact text, a JSON array
    [[nodiscard]] std::expected<String, ControlError> ActionsText(const IControllableGame& game);

    /// WriteReplayStart() as compact text, a JSON object
    [[nodiscard]] std::expected<String, ControlError> ReplayStartText(const IControllableGame& game);

    /// PerformAction() with @p actionJson (one JSON value); the text the game wrote as its outcome
    [[nodiscard]] std::expected<String, ControlError> PerformActionText(IControllableGame& game, StringView actionJson);

    /// Reset() with @p optionsJson, an object
    std::expected<void, ControlError> ResetText(IControllableGame& game, StringView optionsJson = "{}");

    /// SetState() with @p stateJson
    std::expected<void, ControlError> SetStateText(IControllableGame& game, StringView stateJson);

}  // namespace BECore
