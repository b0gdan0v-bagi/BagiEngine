#pragma once

#include <BECore/Control/JsonWriter.h>
#include <Common/JsonDoc.h>
#include <RoomServer/DemoCardGame.h>

namespace BECore::Tests {

    /// The game of the Rooms tests is the demo game of the room server (RoomServer/DemoCardGame.h): a hidden-information card game for four seats
    using FakeSeatedGame = DemoCardGame;

    /// The negative control: the same game whose view also carries every hand ("allHands"). A leak check must catch it
    using LeakyFakeSeatedGame = LeakyDemoCardGame;

    /// The action of the game that plays @p card, as the compact text a client sends (the keys in byte order), and as the document a room is handed
    [[nodiscard]] inline String CardActionText(const String& card) {
        String text;
        JsonWriter writer(text);
        writer.BeginObject();
        writer.Member("card", card);
        writer.Member("type", "play");
        writer.EndObject();
        return text;
    }
    [[nodiscard]] inline JsonDoc CardActionDoc(const String& card) {
        return JsonDoc(CardActionText(card));
    }

    /// The calls into `ISeatedGame` that take a view, driven by a text: the text is taped for the call only
    [[nodiscard]] inline std::expected<void, ControlError> StartText(ISeatedGame& game, uint64_t seed, StringView options) {
        const JsonDoc document(options);
        return game.Start(seed, document);
    }
    [[nodiscard]] inline std::expected<UniquePtr<SeatedOutcome>, ControlError> PerformText(ISeatedGame& game, int seat, StringView action) {
        const JsonDoc document(action);
        return game.PerformSeatAction(seat, document);
    }

    /// The text the computer of @p seat writes, or nullopt when the game says the seat cannot act (then nothing counts of what it wrote)
    [[nodiscard]] inline Optional<String> ChooseComputerText(const ISeatedGame& game, int seat) {
        String text;
        JsonWriter writer(text);
        if (!game.ChooseComputerAction(seat, writer) || !writer.IsComplete()) {
            return std::nullopt;
        }
        return text;
    }

    // ViewText, ActionsText, OutcomeText, Dump and ExpectNoLeak are defined in the engine's test executable only: a game cannot call them

    /// The answers of a seated game as the compact text they are written as: what a client of the wire sees
    [[nodiscard]] String ViewText(const SeatedView& view);
    [[nodiscard]] String ActionsText(const SeatedActions& actions);
    [[nodiscard]] String OutcomeText(const SeatedOutcome& outcome);

    /// The compact text of an answer of a game, or `<error>` for an error (for a leak check and a failure message)
    [[nodiscard]] String Dump(const std::expected<UniquePtr<SeatedView>, ControlError>& view);
    [[nodiscard]] String Dump(const std::expected<UniquePtr<SeatedActions>, ControlError>& actions);
    [[nodiscard]] String Dump(const std::expected<UniquePtr<SeatedOutcome>, ControlError>& outcome);

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
