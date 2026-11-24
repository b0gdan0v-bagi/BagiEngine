#include "FakeSeatedGame.h"

#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        template <typename TAnswer>
        String WrittenAnswer(const TAnswer& answer) {
            String text;
            JsonWriter writer(text);
            answer.WriteJson(writer);
            return text;
        }

        template <typename TAnswer>
        String DumpAnswer(const std::expected<UniquePtr<TAnswer>, ControlError>& answer) {
            return answer && *answer ? WrittenAnswer(**answer) : String("<error>");
        }

    }  // namespace

    String ViewText(const SeatedView& view) {
        return WrittenAnswer(view);
    }

    String ActionsText(const SeatedActions& actions) {
        return WrittenAnswer(actions);
    }

    String OutcomeText(const SeatedOutcome& outcome) {
        return WrittenAnswer(outcome);
    }

    String Dump(const std::expected<UniquePtr<SeatedView>, ControlError>& view) {
        return DumpAnswer(view);
    }

    String Dump(const std::expected<UniquePtr<SeatedActions>, ControlError>& actions) {
        return DumpAnswer(actions);
    }

    String Dump(const std::expected<UniquePtr<SeatedOutcome>, ControlError>& outcome) {
        return DumpAnswer(outcome);
    }

    void ExpectNoLeak(const String& text, int seat, const FakeSeatedGame& game) {
        const Vector<String> leaked = FindLeakedCards(text, seat, game);
        if (leaked.empty()) {
            return;
        }
        String names;
        for (const String& card : leaked) {
            names += card;
            names += ' ';
        }
        ADD_FAILURE() << "a message for seat " << seat << " names the hidden cards " << names.c_str() << ": " << text.c_str();
    }

}  // namespace BECore::Tests
