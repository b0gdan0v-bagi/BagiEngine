#pragma once

#include <BECore/Control/Json.h>
#include <Games/IControllableGame.h>

namespace BECore {

    /// Error codes of a seated game, beside GameControlErrors (a game uses kIllegalAction of that one for a move the rules refuse)
    namespace SeatedGameErrors {
        /// The seat is well-formed but it is not the seat's turn to act
        inline constexpr eastl::string_view kNotYourTurn = "not_your_turn";
        /// The seat number is outside 0..GetSeatCount()-1
        inline constexpr eastl::string_view kInvalidSeat = "invalid_seat";
    }  // namespace SeatedGameErrors

    /**
     * @brief A turn-based game for several seats where each seat may see only its own part of the state (cards in hand, a hidden
     * tile rack). The contract between a game and a room server: the server owns the game, every client is a seat.
     *
     * Unlike IControllableGame (one controller sees everything), everything here is per seat:
     *  - GetSeatView() is what that seat may see and must contain **no hidden information** of the other seats, not even in
     *    a field a client does not draw: the server sends the view to the client as it is;
     *  - GetSeatActions() and PerformSeatAction() name the seat that acts, and the game validates the seat, so a client
     *    cannot act for another one;
     *  - ChooseComputerAction() is how a computer plays an empty or abandoned seat. The server sends the result through
     *    PerformSeatAction() like a person's action, so a computer cannot do what a seat may not.
     *
     * Everything that comes from a client (the seat number of a message, `options`, `action`) is untrusted: read it with the
     * checked helpers of BECore/Control/Json.h, never with the throwing accessors of nlohmann (BECore/Control/CLAUDE.md), range-check
     * numbers as int64_t before narrowing and answer a bad value with an error, not an assert. A seat number may be any
     * integer: the methods answer invalid_seat for one outside the range.
     *
     * One thread at a time: the server serializes every call. Not a BE_CLASS: no scene, no reflection; a game is created by name
     * through SeatedGameRegistry (Games/SeatedGameRegistry.h).
     */
    class ISeatedGame {
    public:
        virtual ~ISeatedGame() = default;

        /// How many seats the game has (fixed for the life of the object)
        [[nodiscard]] virtual int GetSeatCount() const = 0;

        /**
         * @brief Starts a new game (deals, sets the first turn). The same seed gives the same game.
         *
         * @param seed Randomness for the deal, chosen by the server.
         * @param options An object whose members are the game's (rules variant, ...); it comes from a client and may be empty.
         * @return invalid_params for a member that is malformed; the game is then unchanged.
         */
        virtual std::expected<void, ControlError> Start(uint64_t seed, const Json& options) = 0;

        /**
         * @brief What @p seat may see now: its own hidden parts, public parts, the count of the hidden parts of the others.
         *
         * Never the hidden parts of another seat. Valid before Start() too (then an empty object is fine). invalid_seat for
         * a seat out of range.
         */
        [[nodiscard]] virtual std::expected<Json, ControlError> GetSeatView(int seat) const = 0;

        /**
         * @brief A JSON array of what @p seat may send now; each element is accepted as is by PerformSeatAction().
         *
         * Empty when the seat cannot act (not its turn, the game is finished or not started). invalid_seat for a seat out of range.
         */
        [[nodiscard]] virtual std::expected<Json, ControlError> GetSeatActions(int seat) const = 0;

        /**
         * @brief Applies @p action for @p seat after checking it is the seat's to make.
         *
         * @param seat Checked by the game: invalid_seat when out of range.
         * @param action An object from a client. Malformed (no `type`, wrong types): invalid_params.
         * @return What the action did, as a JSON object (public information only, it may be sent to every seat), or an error:
         *         not_your_turn when the seat cannot act now, illegal_action when the action is well-formed but the rules
         *         refuse it (including a card that belongs to another seat). Nothing changes on an error.
         */
        virtual std::expected<Json, ControlError> PerformSeatAction(int seat, const Json& action) = 0;

        /**
         * @brief The action a computer would send for @p seat now, or nullopt when the seat cannot act now (or is out of range).
         *
         * It has to be accepted by PerformSeatAction() for the same seat.
         */
        [[nodiscard]] virtual Optional<Json> ChooseComputerAction(int seat) const = 0;

        /// True once the game is over; then no seat has an action
        [[nodiscard]] virtual bool IsFinished() const = 0;

        /// Grows with every committed action and with Start(); never goes back. The server compares it to detect a stale action
        [[nodiscard]] virtual uint64_t GetStateVersion() const = 0;
    };

}  // namespace BECore
