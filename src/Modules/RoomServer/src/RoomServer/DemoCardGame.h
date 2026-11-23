#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <Games/ISeatedGame.h>
#include <cstdint>

namespace BECore {

    // The answers of DemoCardGame: `BE_CLASS` structs of plain `Vector` / `String` fields (the room carries them from the game to the hub, so no arena
    // type). Their keys are the wire's: the tests compare the written text with a hand-built `Json` reference.

    /// A card on the table and the seat that played it
    struct DemoTablePlay {
        BE_CLASS(DemoTablePlay)

        BE_REFLECT_FIELD String card;
        BE_REFLECT_FIELD int32_t seat = 0;
    };

    /// What one seat sees: its own hand, only the size of every hand, the played cards. `turn` is -1 when the game is finished
    struct DemoSeatView : SeatedView {
        BE_CLASS(DemoSeatView)

        BE_REFLECT_FIELD Vector<int32_t> cardCounts;
        BE_REFLECT_FIELD bool finished = false;
        BE_REFLECT_FIELD Vector<String> hand;
        BE_REFLECT_FIELD int32_t seat = 0;
        BE_REFLECT_FIELD Vector<DemoTablePlay> table;
        BE_REFLECT_FIELD int32_t turn = -1;
    };

    /// Playing one card of the hand: what a client sends back as `action`
    struct DemoSeatAction : SeatedAction {
        BE_CLASS(DemoSeatAction)

        BE_REFLECT_FIELD String card;
        BE_REFLECT_FIELD String type;
    };

    /// The seat that played and the card
    struct DemoSeatOutcome : SeatedOutcome {
        BE_CLASS(DemoSeatOutcome)

        BE_REFLECT_FIELD String card;
        BE_REFLECT_FIELD int32_t seat = 0;
    };

    /// One hand of the negative control's view
    struct LeakyHand {
        BE_CLASS(LeakyHand)

        BE_REFLECT_FIELD Vector<String> cards;
    };

    /// The negative control's view: the view of the seat with every hand in it, which a leak check must catch
    struct LeakyDemoSeatView : DemoSeatView {
        BE_CLASS(LeakyDemoSeatView)

        BE_REFLECT_FIELD Vector<LeakyHand> allHands;
    };

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
     *
     * The view, the actions and the outcome are the reflected structs above; the server writes them through their generated `WriteJson`.
     */
    class DemoCardGame : public ISeatedGame {
    public:
        static constexpr int kSeatCount = 4;
        static constexpr int kCardsPerSeat = 3;
        static constexpr int kTotalPlays = kSeatCount * kCardsPerSeat;

        ~DemoCardGame() override = default;

        int GetSeatCount() const override;
        std::expected<void, ControlError> Start(uint64_t seed, const Json& options) override;
        std::expected<UniquePtr<SeatedView>, ControlError> GetSeatView(int seat) const override;
        std::expected<UniquePtr<SeatedActions>, ControlError> GetSeatActions(int seat) const override;
        std::expected<UniquePtr<SeatedOutcome>, ControlError> PerformSeatAction(int seat, const Json& action) override;
        Optional<Json> ChooseComputerAction(int seat) const override;
        bool IsFinished() const override;
        uint64_t GetStateVersion() const override;

        /// Test access to the truth, not part of the contract: the cards @p seat still holds (empty for a seat out of range)
        [[nodiscard]] const Vector<String>& GetHand(int seat) const;
        /// The seat whose turn it is (-1 before Start and after the end)
        [[nodiscard]] int GetTurn() const;
        [[nodiscard]] int GetPlayCount() const;

    protected:
        /// The view of a valid @p seat as @p TView (DemoSeatView or a type derived from it); the leaky game fills its extra field on top
        template <typename TView>
        [[nodiscard]] UniquePtr<TView> MakeSeatView(int seat) const;

    private:
        [[nodiscard]] bool CanAct(int seat) const;

        Vector<String> _hands[kSeatCount];
        struct Play {
            int seat = 0;
            String card;
        };
        Vector<Play> _table;
        int _turn = -1;
        bool _started = false;
        uint64_t _version = 0;
    };

    /// The negative control: the same game whose view also carries every hand ("allHands"). A leak check must catch it. `--test-games` registers it beside `cards`
    class LeakyDemoCardGame : public DemoCardGame {
    public:
        std::expected<UniquePtr<SeatedView>, ControlError> GetSeatView(int seat) const override;
    };

    /// The card ids that @p text mentions and that sit in the hand of a seat other than @p seat in @p game right now
    [[nodiscard]] Vector<String> FindHiddenCards(const String& text, int seat, const DemoCardGame& game);

}  // namespace BECore
