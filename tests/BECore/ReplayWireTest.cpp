// The mismatch of `replay` is a reflected struct (ReplayAnswers.h); the DOM of `ReplayMismatchJson` stays for the tests that read it. Their bytes must be one.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Memory/ScratchArena.h>
#include <Common/Printers.h>
#include <Games/ReplayAnswers.h>
#include <Games/ReplayPlayer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ReplayMismatch MakeMismatch(ReplayMismatchKind kind) {
            ReplayMismatch mismatch;
            mismatch.index = 3;
            mismatch.kind = kind;
            mismatch.entry.op = ReplayOp::Action;
            mismatch.entry.params = Json::parse(R"({"type":"roll","z":[1,2]})");
            mismatch.entry.outcome = Json::parse(R"({"value":4})");
            mismatch.entry.gap = ReplayGap{2, 0.5};
            mismatch.entry.line = 5;
            return mismatch;
        }

        template <typename T>
        String Written(const T& object) {
            String out;
            JsonWriter writer(out);
            object.WriteJson(writer);
            return out;
        }

        String AnswerText(const ReplayMismatch& mismatch) {
            ScratchScope scratch;  // the error of the answer is on the scratch arena
            ReplayMismatchAnswer answer;
            FillReplayMismatchAnswer(answer, mismatch);
            return Written(answer);
        }

    }  // namespace

    TEST(ReplayWireTest, ARefusedEntryIsTheBytesOfTheDomWithTheError) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::Refused);
        mismatch.error = MakeControlError("illegal_action", "the lock \"arm\" is not over");

        EXPECT_EQ(AnswerText(mismatch), DumpJson(ReplayMismatchJson(mismatch)));
    }

    TEST(ReplayWireTest, ADifferingOutcomeIsTheBytesOfTheDomWithExpectedAndActual) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::OutcomeDiffers);
        mismatch.expected = Json::parse(R"({"value":4})");
        mismatch.actual = Json::parse(R"({"value":5,"a":null})");

        EXPECT_EQ(AnswerText(mismatch), DumpJson(ReplayMismatchJson(mismatch)));
    }

    TEST(ReplayWireTest, ADifferingStartIsTheBytesOfTheDomWithTheParamsOfTheEntryAndTheStart) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::StartDiffers);
        mismatch.entry.op = ReplayOp::Reset;
        mismatch.entry.outcome.reset();
        mismatch.expected = Json::parse(R"({"seed":21})");
        mismatch.actual = Json::parse(R"({"seed":1})");

        EXPECT_EQ(AnswerText(mismatch), DumpJson(ReplayMismatchJson(mismatch)));
    }

    TEST(ReplayWireTest, TheMismatchSitsUnderItsKeyInTheAnswerAndOnlyWhenThereIsOne) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::OutcomeDiffers);
        mismatch.expected = Json::parse(R"({"value":4})");
        mismatch.actual = Json::parse(R"({"value":5})");
        ScratchScope scratch;  // the error of the mismatch is on the scratch arena
        ReplayAnswer failed;
        failed.entries = 3;
        FillReplayMismatchAnswer(failed.mismatch.emplace(), mismatch);
        const ReplayAnswer played;

        const auto failedJson = ParseJson(Written(failed));
        const auto playedJson = ParseJson(Written(played));

        ASSERT_TRUE(failedJson.has_value());
        ASSERT_TRUE(playedJson.has_value());
        const Json* inAnswer = FindJsonMember(*failedJson, "mismatch");
        ASSERT_NE(inAnswer, nullptr);
        EXPECT_EQ(*inAnswer, ReplayMismatchJson(mismatch));
        EXPECT_EQ(FindJsonMember(*playedJson, "mismatch"), nullptr);
    }

}  // namespace BECore::Tests
