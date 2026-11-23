// The mismatch of `replay` is a reflected struct (ReplayAnswers.h); its bytes are pinned here, `ReplayMismatchText` writes the same document.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriteReflect.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/Printers.h>
#include <Games/ReplayAnswers.h>
#include <Games/ReplayPlayer.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The mismatch of an action entry at line 5; the entry text is what the player makes once
        ReplayMismatch MakeMismatch(ReplayMismatchKind kind) {
            ReplayMismatch mismatch;
            mismatch.index = 3;
            mismatch.kind = kind;
            mismatch.entry.op = ReplayOp::Action;
            mismatch.entry.params = R"({"type":"roll","z":[1,2]})";
            mismatch.entry.outcome = String(R"({"value":4})");
            mismatch.entry.gap = ReplayGap{2, 0.5};
            mismatch.entry.line = 5;
            mismatch.entryText = FormatReplayEntry(mismatch.entry);
            return mismatch;
        }

        template <typename T>
        String Written(const T& object) {
            String out;
            JsonWriter writer(out);
            object.WriteJson(writer);
            return out;
        }

    }  // namespace

    TEST(ReplayWireTest, ARefusedEntryIsTheEntryAsALineAndTheError) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::Refused);
        mismatch.error = MakeControlError("illegal_action", "the lock \"arm\" is not over");

        EXPECT_EQ(ReplayMismatchText(mismatch), R"({"entry":{"op":"action","params":{"type":"roll","z":[1,2]},"outcome":{"value":4},"gap":{"frames":2,"seconds":0.5}},)"
                                                R"("error":{"code":"illegal_action","message":"the lock \"arm\" is not over"},"index":3,"kind":"refused","line":5})");
    }

    TEST(ReplayWireTest, ADifferingOutcomeCarriesExpectedAndActualAsTheyAre) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::OutcomeDiffers);
        mismatch.expected = String(R"({"value":4})");
        mismatch.actual = String(R"({"a":null,"value":5})");

        EXPECT_EQ(ReplayMismatchText(mismatch), R"({"actual":{"a":null,"value":5},"entry":{"op":"action","params":{"type":"roll","z":[1,2]},"outcome":{"value":4},"gap":{"frames":2,"seconds":0.5}},)"
                                                R"("expected":{"value":4},"index":3,"kind":"outcome_differs","line":5})");
    }

    TEST(ReplayWireTest, ADifferingStartCarriesTheParamsOfTheEntryAndTheStart) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::StartDiffers);
        mismatch.entry.op = ReplayOp::Reset;
        mismatch.entry.outcome.reset();
        mismatch.entryText = FormatReplayEntry(mismatch.entry);
        mismatch.expected = String(R"({"seed":21})");
        mismatch.actual = String(R"({"seed":1})");

        EXPECT_EQ(ReplayMismatchText(mismatch), R"({"actual":{"seed":1},"entry":{"op":"reset","params":{"type":"roll","z":[1,2]},"gap":{"frames":2,"seconds":0.5}},)"
                                                R"("expected":{"seed":21},"index":3,"kind":"start_differs","line":5})");
    }

    TEST(ReplayWireTest, TheMismatchSitsUnderItsKeyInTheAnswerAndOnlyWhenThereIsOne) {
        ReplayMismatch mismatch = MakeMismatch(ReplayMismatchKind::OutcomeDiffers);
        mismatch.expected = String(R"({"value":4})");
        mismatch.actual = String(R"({"value":5})");
        ReplayAnswer failed;
        failed.entries = 3;
        FillReplayMismatchAnswer(failed.mismatch.emplace(), mismatch);
        const ReplayAnswer played;

        EXPECT_NE(Written(failed).find(Format("\"mismatch\":{}", ReplayMismatchText(mismatch))), String::npos);
        EXPECT_EQ(Written(played).find("mismatch"), String::npos);
    }

}  // namespace BECore::Tests
