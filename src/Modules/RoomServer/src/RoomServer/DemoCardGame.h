#pragma once

#include <Games/ISeatedGame.h>

namespace BECore {

    /**
     * @brief A hidden-information card game for four seats: the test and demo game of the room server, not for a product.
     *
     * The Rooms tests, the socket scenarios and the smoke test of `BagiRoomServer --test-games` play it. A product game brings its own
     * ISeatedGame and its own server `main`.
     *
     * Each seat holds three secret cards with the unique ids `c<seat>_<n>` (n 0..2). On its turn a seat plays one of its
     * cards (action {"type":"play","card":"c1_2"}); the turn passes to the next seat. After 12 plays every card is on the
     * table, the game is finished and nobody has an action. Start(seed, options) deals deterministically: the seed shuffles
     * the order of each hand and picks the first seat (options {"firstSeat": 0..3} overrides it).
     *
     * The view of a seat is {"seat", "hand", "cardCounts", "table", "turn", "finished"}: its own cards, how many cards every
     * seat holds, the cards played (seat and card) and whose turn it is, and nothing else. ChooseComputerAction plays the
     * lowest card id of the hand.
     */
    class DemoCardGame : public ISeatedGame {
    public:
        static constexpr int kSeatCount = 4;
        static constexpr int kCardsPerSeat = 3;
        static constexpr int kTotalPlays = kSeatCount * kCardsPerSeat;

        ~DemoCardGame() override = default;

        int GetSeatCount() const override;
        std::expected<void, ControlError> Start(uint64_t seed, const Json& options) override;
        std::expected<Json, ControlError> GetSeatView(int seat) const override;
        std::expected<Json, ControlError> GetSeatActions(int seat) const override;
        std::expected<Json, ControlError> PerformSeatAction(int seat, const Json& action) override;
        Optional<Json> ChooseComputerAction(int seat) const override;
        bool IsFinished() const override;
        uint64_t GetStateVersion() const override;

        /// Test access to the truth, not part of the contract: the cards @p seat still holds (empty for a seat out of range)
        [[nodiscard]] const eastl::vector<eastl::string>& GetHand(int seat) const;
        /// The seat whose turn it is (-1 before Start and after the end)
        [[nodiscard]] int GetTurn() const;
        [[nodiscard]] int GetPlayCount() const;

    private:
        [[nodiscard]] bool CanAct(int seat) const;

        eastl::vector<eastl::string> _hands[kSeatCount];
        struct Play {
            int seat = 0;
            eastl::string card;
        };
        eastl::vector<Play> _table;
        int _turn = -1;
        bool _started = false;
        uint64_t _version = 0;
    };

    /// The negative control: the same game whose view also carries every hand ("allHands"). A leak check must catch it. Only `--test-games` registers it
    class LeakyDemoCardGame : public DemoCardGame {
    public:
        std::expected<Json, ControlError> GetSeatView(int seat) const override;
    };

    /// The card ids that @p text mentions and that sit in the hand of a seat other than @p seat in @p game right now
    [[nodiscard]] eastl::vector<eastl::string> FindHiddenCards(const eastl::string& text, int seat, const DemoCardGame& game);

}  // namespace BECore
