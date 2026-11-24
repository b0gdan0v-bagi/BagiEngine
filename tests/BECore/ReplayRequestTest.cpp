// The two halves of the application's own `replay` request (Application/ReplayRequest.h): the request text and the reading of its answer.

#include <Application/ReplayRequest.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(ReplayRequestTest, TheRequestIsTheTextTheDomWrote) {
        EXPECT_EQ(MakeReplayRequestText("runs/a b.jsonl"), R"({"id":1,"method":"replay","params":{"path":"runs/a b.jsonl"}})");
    }

    TEST(ReplayRequestTest, ThePathIsEscapedLikeAnyString) {
        EXPECT_EQ(MakeReplayRequestText("C:\\runs\\\"q\".jsonl"), R"({"id":1,"method":"replay","params":{"path":"C:\\runs\\\"q\".jsonl"}})");
    }

    TEST(ReplayRequestTest, AnOkResultIsPlayedWithItsEntries) {
        const ReplayOutcome outcome = ReadReplayAnswer(R"({"id":1,"ok":true,"result":{"entries":12,"ok":true}})");

        EXPECT_EQ(outcome.kind, ReplayOutcomeKind::Played);
        EXPECT_EQ(outcome.entries, 12);
    }

    TEST(ReplayRequestTest, AMismatchIsAFailureNamedByTheTextOfTheMismatch) {
        const ReplayOutcome outcome = ReadReplayAnswer(R"({"id":1,"ok":true,"result":{"entries":3,"mismatch":{"entry":2,"why":"state differs"},"ok":false}})");

        EXPECT_EQ(outcome.kind, ReplayOutcomeKind::Failed);
        EXPECT_TRUE(outcome.isMismatch);
        EXPECT_EQ(outcome.detail, R"({"entry":2,"why":"state differs"})");
    }

    TEST(ReplayRequestTest, AnErrorIsAFailureNamedByTheTextOfTheError) {
        const ReplayOutcome outcome = ReadReplayAnswer(R"({"id":1,"error":{"code":"invalid_params","message":"'path' is required"},"ok":false})");

        EXPECT_EQ(outcome.kind, ReplayOutcomeKind::Failed);
        EXPECT_FALSE(outcome.isMismatch);
        EXPECT_EQ(outcome.detail, R"({"code":"invalid_params","message":"'path' is required"})");
    }

    TEST(ReplayRequestTest, TheShuttingDownErrorIsNotAFailure) {
        const ReplayOutcome outcome = ReadReplayAnswer(R"({"id":1,"error":{"code":"shutting_down","message":"the application is ending"},"ok":false})");

        EXPECT_EQ(outcome.kind, ReplayOutcomeKind::ShuttingDown);
    }

    TEST(ReplayRequestTest, AnAnswerThatIsNotAnAnswerIsAFailureNamedByTheWholeText) {
        for (const StringView text : {StringView("not json"), StringView(""), StringView("[1]"), StringView(R"({"id":1,"ok":false})")}) {
            const ReplayOutcome outcome = ReadReplayAnswer(text);

            EXPECT_EQ(outcome.kind, ReplayOutcomeKind::Failed) << String(text).c_str();
            EXPECT_FALSE(outcome.isMismatch);
            EXPECT_EQ(outcome.detail, text);
        }
    }

}  // namespace BECore::Tests
