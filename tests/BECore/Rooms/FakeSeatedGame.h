#pragma once

#include <Games/ISeatedGame.h>

namespace BECore::Tests {

    /**
     * @brief A hidden-information card game for four seats, the test double of every Rooms test.
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
    class FakeSeatedGame : public ISeatedGame {
    public:
        static constexpr int kSeatCount = 4;
        static constexpr int kCardsPerSeat = 3;
        static constexpr int kTotalPlays = kSeatCount * kCardsPerSeat;

        ~FakeSeatedGame() override = default;

        int GetSeatCount() const override;
        std::expected<void, ControlError> Start(uint64_t seed, const Json& options) override;
        std::expected<Json, ControlError> GetSeatView(int seat) const override;
        std::expected<Json, ControlError> GetSeatActions(int seat) const override;
        std::expected<Json, ControlError> PerformSeatAction(int seat, const Json& action) override;
        std::optional<Json> ChooseComputerAction(int seat) const override;
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

    /// The negative control: the same game whose view also carries every hand ("allHands"). A leak check must catch it.
    class LeakyFakeSeatedGame : public FakeSeatedGame {
    public:
        std::expected<Json, ControlError> GetSeatView(int seat) const override;
    };

    /// The card ids that @p text mentions and that sit in the hand of a seat other than @p seat in @p game right now
    [[nodiscard]] eastl::vector<eastl::string> FindLeakedCards(const eastl::string& text, int seat, const FakeSeatedGame& game);

    /**
     * @brief Fails the test when @p text (a message sent to @p seat) names a card that is hidden from that seat.
     *
     * "Hidden" is judged by the game now: call it right after the message was produced, while the cards are still in the hands
     * (a card that has been played is public, so a check made after the end finds nothing).
     */
    void ExpectNoLeak(const eastl::string& text, int seat, const FakeSeatedGame& game);

}  // namespace BECore::Tests
