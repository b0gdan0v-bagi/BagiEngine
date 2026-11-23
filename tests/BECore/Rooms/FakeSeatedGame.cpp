#include "FakeSeatedGame.h"

#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A null Json when the text does not parse, which every comparison of a test then reports
        template <typename TAnswer>
        Json WrittenJson(const TAnswer& answer) {
            String text;
            JsonWriter writer(text);
            answer.WriteJson(writer);
            auto parsed = ParseJson(text);
            return parsed ? std::move(*parsed) : Json();
        }

        template <typename TAnswer>
        String DumpAnswer(const std::expected<UniquePtr<TAnswer>, ControlError>& answer) {
            return answer && *answer ? DumpJson(WrittenJson(**answer)) : String("<error>");
        }

    }  // namespace

    Json ViewJson(const SeatedView& view) {
        return WrittenJson(view);
    }

    Json ActionsJson(const SeatedActions& actions) {
        return WrittenJson(actions);
    }

    Json OutcomeJson(const SeatedOutcome& outcome) {
        return WrittenJson(outcome);
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
