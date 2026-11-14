#pragma once

#include <RoomServer/DemoCardGame.h>

namespace BECore::Tests {

    /// The game of the Rooms tests is the demo game of the room server (RoomServer/DemoCardGame.h): a hidden-information card game for four seats
    using FakeSeatedGame = DemoCardGame;

    /// The negative control: the same game whose view also carries every hand ("allHands"). A leak check must catch it
    using LeakyFakeSeatedGame = LeakyDemoCardGame;

    /// The card ids that @p text mentions and that sit in the hand of a seat other than @p seat in @p game right now
    [[nodiscard]] inline Vector<String> FindLeakedCards(const String& text, int seat, const FakeSeatedGame& game) {
        return FindHiddenCards(text, seat, game);
    }

    /**
     * @brief Fails the test when @p text (a message sent to @p seat) names a card that is hidden from that seat.
     *
     * "Hidden" is judged by the game now: call it right after the message was produced, while the cards are still in the hands
     * (a card that has been played is public, so a check made after the end finds nothing).
     */
    void ExpectNoLeak(const String& text, int seat, const FakeSeatedGame& game);

}  // namespace BECore::Tests
