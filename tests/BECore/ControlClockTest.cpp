// ControlClock: the clock of an answer, written by the generated writer, and ControlCall::ReplyObject.

#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlClock.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/ControlClockTypes.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Common/ControlTestSupport.h>
#include <Generated/ControlClockTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kFrameSeconds = 0.125;

        template <typename T>
        String Written(const T& value) {
            String text;
            JsonWriter writer(text);
            value.WriteJson(writer);
            EXPECT_TRUE(writer.IsComplete());
            return text;
        }

        class ControlClockTest : public ::testing::Test {
        protected:
            void SetUp() override {
                CoreManager::GetTime().Reset();
                CoreManager::GetTime().Advance(kFrameSeconds);
                CoreManager::GetTime().Advance(kFrameSeconds);
            }

            void TearDown() override {
                CoreManager::GetTime().Reset();
            }
        };

    }  // namespace

    TEST_F(ControlClockTest, TheReadClockIsWrittenAsTheDomPathWroteIt) {
        EXPECT_EQ(Written(ControlClock::Read()), DumpJson(DomClock()));
    }

    TEST_F(ControlClockTest, AnAnswerThatDerivesTheClockMergesItsKeysSorted) {
        ControlClockTestAnswer answer;
        static_cast<ControlClock&>(answer) = ControlClock::Read();

        Json expected = DomClock();
        expected["a"] = 1;
        expected["fz"] = 2;
        expected["zz"] = 3;
        EXPECT_EQ(Written(answer), DumpJson(expected));
    }

    TEST_F(ControlClockTest, ReplyObjectAnswersWithTheWrittenValueAsTheResult) {
        ControlService service;
        service.RegisterHandler("clock", [](ControlCall& call) { call.ReplyObject(ControlClock::Read()); });

        String text;
        service.Post(MakeRequest(4, "clock"), [&text](const String& reply) { text = reply; });
        service.Drain();

        EXPECT_EQ(text, MakeOkResponse(Json(4), DomClock()));
    }

}  // namespace BECore::Tests
